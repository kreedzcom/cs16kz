BEGIN;
INSERT INTO game_server (name, access_key)
VALUES ('kz-e2e-server', decode(:'token', 'hex'));

INSERT INTO plugin_version (semver, checksum_linux, checksum_windows)
VALUES (:'version', decode(:'checksum', 'hex'), decode(:'checksum', 'hex'));

INSERT INTO map (name, type, length_tier, difficulty)
VALUES ('kz_e2e_first', 'climb', 2, 3),
       ('kz_e2e_second', 'bhop', 4, 7);
COMMIT;
