package kz.global.api.ws.handlers

import io.micrometer.core.instrument.simple.SimpleMeterRegistry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.json.*
import kz.global.api.db.tables.GameServersTable
import kz.global.api.db.tables.MapRecordsTable
import kz.global.api.db.tables.PluginVersionsTable
import kz.global.api.db.tables.WorldRecordsTable
import kz.global.api.domain.players.PlayerBanService
import kz.global.api.domain.records.RecordService
import kz.global.api.domain.replays.FakeR2Client
import kz.global.api.domain.replays.ReplayService
import kz.global.api.events.AuditLogger
import kz.global.api.events.KzEventBus
import kz.global.api.metrics.KzMetrics
import kz.global.api.support.TestDatabase
import kz.global.api.support.mockSession
import kz.global.api.support.testSecurityConfig
import kz.global.api.support.testWsRateLimiters
import kz.global.api.support.testWsRateLimitersStrict
import kz.global.api.ws.AddRecordPayload
import kz.global.api.ws.ConnectedServersRegistry
import kz.global.api.ws.MsgType
import kz.global.api.ws.WsEnvelope
import org.jetbrains.exposed.v1.jdbc.insert
import org.jetbrains.exposed.v1.jdbc.selectAll
import org.jetbrains.exposed.v1.jdbc.transactions.transaction
import java.io.File
import kotlin.test.*
import kotlin.uuid.Uuid

class NativeReplayContractTest {

    @Test
    fun `native chunks survive repeated submission reconnect and assembly restart`() = runTest {
        val fixtures = File(checkNotNull(System.getenv("KZ_CONTRACT_FIXTURES")))
        TestDatabase.connect()
        TestDatabase.truncateAll()
        val (server, version) = transaction {
            val server = GameServersTable.insert {
                it[name] = "contract-server"
                it[accessKey] = ByteArray(16)
            }[GameServersTable.id]
            val version = PluginVersionsTable.insert {
                it[semver] = "contract"
                it[checksumLinux] = ByteArray(16)
                it[checksumWindows] = ByteArray(16)
            }[PluginVersionsTable.id]
            server to version
        }
        val security = testSecurityConfig()
        val metrics = KzMetrics(SimpleMeterRegistry(), ConnectedServersRegistry())
        val r2 = FakeR2Client()
        val dispatcher = Dispatchers.Unconfined
        val records = RecordService(KzEventBus(), AuditLogger(), metrics, PlayerBanService(AuditLogger()), security, dispatcher)
        val normal = AddRecordHandler(records, testWsRateLimiters())
        val rateLimited = AddRecordHandler(records, testWsRateLimitersStrict())
        fun replayHandler() = ReplayChunkHandler(
            ReplayService(r2, metrics, security, dispatcher), records, metrics, testWsRateLimiters(), dispatcher,
        )
        val pro = AddRecordPayload("STEAM_0:0:88888", "kz_contract", 20_000, "0_00020000_0088888_contract", 0, 0)
        val nub = pro.copy(timeMs = 25_000, localUid = "1_00025000_0088888_contract", checkpoints = 1, gochecks = 1)
        fun envelope(id: Long, payload: AddRecordPayload) = WsEnvelope(MsgType.ADD_RECORD, id, Json.encodeToJsonElement(payload))
        val (first, firstReplies) = mockSession(server, version)

        rateLimited.handle(first, envelope(1, pro))
        rateLimited.handle(first, envelope(2, nub))
        normal.handle(first, envelope(3, nub))
        val firstProId = firstReplies().first().data.jsonObject.getValue("id")
        val (reconnected, replies) = mockSession(server, version)
        normal.handle(reconnected, envelope(4, pro))
        val stillSame = replies().first().data.jsonObject.getValue("id")
        val stagedProId = Uuid.parse(firstProId.jsonPrimitive.content)
        val interruptedStage = ReplayService(r2, metrics, security, dispatcher)
            .stageReplay(stagedProId, File(fixtures, "pro.krpz").readBytes())
        for (label in listOf("pro", "nub")) {
            val chunks = fixtures.listFiles { _, filename -> filename.startsWith("$label.") && filename.endsWith(".chunk") }!!
                .sortedBy { it.name.split('.')[1].toInt() }
                .map(File::readBytes)
            assertTrue(chunks.size > 1)
            val beforeRestart = replayHandler()
            beforeRestart.handle(first, chunks.first())
            val afterRestart = replayHandler()
            chunks.forEach { afterRestart.handle(reconnected, it) }
            // Replay a successful upload when the sending peer missed its final ACK.
            chunks.forEach { afterRestart.handle(reconnected, it) }
        }

        assertEquals(firstProId, stillSame)
        assertEquals(MsgType.ERROR, firstReplies()[1].msgType)
        assertTrue(firstReplies()[1].data.jsonObject.getValue("retryable").jsonPrimitive.boolean)
        assertEquals(4, replies().count {
            it.msgType == MsgType.FILE_ACK && it.data.jsonObject.getValue("status").jsonPrimitive.boolean &&
                it.data.jsonObject.getValue("playable").jsonPrimitive.boolean
        })
        assertEquals(3, r2.putCalls.size)
        assertEquals(2, r2.putCalls.count { (key, _) -> key.startsWith("replays/$stagedProId/") })
        assertEquals(3, r2.putCalls.map { it.first }.toSet().size)
        for (label in listOf("pro", "nub")) {
            val expected = File(fixtures, "$label.krpz").readBytes()
            assertTrue(r2.putCalls.any { (_, bytes) -> bytes.contentEquals(expected) })
        }
        transaction {
            assertEquals(2L, MapRecordsTable.selectAll().count())
            assertFalse(MapRecordsTable.selectAll().any { it[MapRecordsTable.replayR2Key] == interruptedStage })
            assertEquals(setOf("pro", "nub"), WorldRecordsTable.selectAll().map { it[WorldRecordsTable.category] }.toSet())
        }
    }
}
