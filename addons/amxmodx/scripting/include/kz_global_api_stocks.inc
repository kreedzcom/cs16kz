#if defined _kz_global_api_stocks_included
  #endinput
#endif
#define _kz_global_api_stocks_included

/**
 * Map types
 */

enum
{
	KZM_BHOP,
	KZM_CLIMB,
	KZM_SLIDE,
	KZM_MIX,
	KZM_SPECIAL,
};

/**
 * Map length
 */
enum
{
	KZM_VERY_SHORT,
	KZM_SHORT,
	KZM_MIDDLE,
	KZM_LONG,
	KZM_VERY_LONG,
};

/**
 * Map difficulty
 */
enum
{
	KZM_BEGINNER,
	KZM_EASY,
	KZM_EASY_MEDIUM,
	KZM_MEDIUM,
	KZM_MEDIUM_HARD,
	KZM_HARD,
	KZM_HARD_EXTREME,
	KZM_EXTREME,
	KZM_EXTREME_DEATH,
	KZM_DEATH,
};

/**
 * Weapons enum for the KZ API.
 */
enum KZWeapons
{
	KZW_AWP = 0,
	KZW_M249,
	KZW_AK47,
	KZW_M4A1,
	KZW_SG552,
	KZW_FAMAS,
	KZW_P90,
	KZW_USP,
	KZW_SCOUT,	
};

/**
 * Returns the string representation of map type enum.
 *
 * @param type          The map type enum
 * @param output        The buffer to store the string
 * @param maxlen        Maximum buffer length
 *
 * @return              0
 */
stock kz_api_get_map_type(type, output[], maxlen)
{
	switch(type)
	{
		case KZM_BHOP:    formatex(output, maxlen, "Bhop");
		case KZM_CLIMB:   formatex(output, maxlen, "Climb");
		case KZM_SLIDE:   formatex(output, maxlen, "Slide");
		case KZM_MIX:     formatex(output, maxlen, "Mix");
		case KZM_SPECIAL: formatex(output, maxlen, "Special");
        default:          formatex(output, maxlen, "Unknown");
	}
	return 0;
}

/**
 * Returns the string representation of map length enum.
 *
 * @param length        The map length enum
 * @param output        The buffer to store the string
 * @param maxlen        Maximum buffer length
 *
 * @return              0
 */
stock kz_api_get_map_length(length, output[], maxlen)
{
	switch(length)
	{
		case KZM_VERY_SHORT: formatex(output, maxlen, "Very Short");
		case KZM_SHORT:      formatex(output, maxlen, "Short");
		case KZM_MIDDLE:     formatex(output, maxlen, "Middle");
		case KZM_LONG:       formatex(output, maxlen, "Long");
		case KZM_VERY_LONG:  formatex(output, maxlen, "Very Long");
        default:             formatex(output, maxlen, "Unknown");
	}
	return 0;
}

/**
 * Returns the string representation of map difficulty enum.
 *
 * @param difficulty    The map difficulty enum
 * @param output        The buffer to store the string
 * @param maxlen        Maximum buffer length
 *
 * @return              0
 */
stock kz_api_get_map_difficulty(difficulty, output[], maxlen)
{
	switch(difficulty)
	{
		case KZM_BEGINNER:      formatex(output, maxlen, "Beginner");
		case KZM_EASY:          formatex(output, maxlen, "Easy");
		case KZM_EASY_MEDIUM:   formatex(output, maxlen, "Easy-Medium");
		case KZM_MEDIUM:        formatex(output, maxlen, "Medium");
		case KZM_MEDIUM_HARD:   formatex(output, maxlen, "Medium-Hard");
		case KZM_HARD:          formatex(output, maxlen, "Hard");
		case KZM_HARD_EXTREME:  formatex(output, maxlen, "Hard-Extreme");
		case KZM_EXTREME:       formatex(output, maxlen, "Extreme");
		case KZM_EXTREME_DEATH: formatex(output, maxlen, "Extreme-Death");
		case KZM_DEATH:         formatex(output, maxlen, "Death");
        default:                formatex(output, maxlen, "Unknown");
	}
	return 0;
}
