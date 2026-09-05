// Author: SBDWolf
// v1.0

state("DeSmuME_0.9.11_x64")
{
}


state("DeSmuME_0.9.13_x64")
{
}

state("MelonDS")
{
}

startup
{
    vars.millisecond_lookup = new int[] {
      0,  17,  33,  50,  67,  83,
    100, 117, 133, 150, 167, 183,
    200, 217, 233, 250, 267, 283,
    300, 317, 333, 350, 367, 383,
    400, 417, 433, 450, 467, 483,
    500, 517, 533, 550, 567, 583,
    600, 617, 633, 650, 667, 683,
    700, 717, 733, 750, 767, 783,
    800, 817, 833, 850, 867, 883,
    900, 917, 933, 950, 967, 983
    };
    vars.second_to_display = 0;
    vars.frames_to_display = 0;

    // part of the dungeon struct. the struct needs to be dereferenced and read during gameplay, and only then can these variables be updated.
    vars.current_dungeon_id = 0;
    vars.current_floor = 0;
    vars.current_floor_old = 0;
    vars.is_clearing_floor = false;

    // credits to: https://github.com/UsernameFodder/pmdsky-debug/blob/master/headers/types/common/enums.h#L2999
    vars.DUNGEON_TEST_DUNGEON = 0;
    vars.DUNGEON_BEACH_CAVE = 1;
    vars.DUNGEON_BEACH_CAVE_PIT = 2;
    vars.DUNGEON_DRENCHED_BLUFF = 3;
    vars.DUNGEON_MT_BRISTLE = 4;
    vars.DUNGEON_MT_BRISTLE_PEAK = 5;
    vars.DUNGEON_WATERFALL_CAVE = 6;
    vars.DUNGEON_APPLE_WOODS = 7;
    vars.DUNGEON_CRAGGY_COAST = 8;
    vars.DUNGEON_SIDE_PATH = 9;
    vars.DUNGEON_MT_HORN = 10;
    vars.DUNGEON_ROCK_PATH = 11;
    vars.DUNGEON_FOGGY_FOREST = 12;
    vars.DUNGEON_FOREST_PATH = 13;
    vars.DUNGEON_STEAM_CAVE = 14;
    vars.DUNGEON_UPPER_STEAM_CAVE = 15;
    vars.DUNGEON_STEAM_CAVE_PEAK = 16;
    vars.DUNGEON_AMP_PLAINS = 17;
    vars.DUNGEON_FAR_AMP_PLAINS = 18;
    vars.DUNGEON_AMP_CLEARING = 19;
    vars.DUNGEON_NORTHERN_DESERT = 20;
    vars.DUNGEON_QUICKSAND_CAVE = 21;
    vars.DUNGEON_QUICKSAND_PIT = 22;
    vars.DUNGEON_UNDERGROUND_LAKE = 23;
    vars.DUNGEON_CRYSTAL_CAVE = 24;
    vars.DUNGEON_CRYSTAL_CROSSING = 25;
    vars.DUNGEON_CRYSTAL_LAKE = 26;
    vars.DUNGEON_CHASM_CAVE = 27;
    vars.DUNGEON_DARK_HILL = 28;
    vars.DUNGEON_SEALED_RUIN = 29;
    vars.DUNGEON_DEEP_SEALED_RUIN = 30;
    vars.DUNGEON_SEALED_RUIN_PIT = 31;
    vars.DUNGEON_DUSK_FOREST = 32;
    vars.DUNGEON_DEEP_DUSK_FOREST = 33;
    vars.DUNGEON_TREESHROUD_FOREST = 34;
    vars.DUNGEON_BRINE_CAVE = 35;
    vars.DUNGEON_LOWER_BRINE_CAVE = 36;
    vars.DUNGEON_BRINE_CAVE_PIT = 37;
    vars.DUNGEON_HIDDEN_LAND = 38;
    vars.DUNGEON_HIDDEN_HIGHLAND = 39;
    vars.DUNGEON_OLD_RUINS = 40;
    vars.DUNGEON_TEMPORAL_TOWER = 41;
    vars.DUNGEON_TEMPORAL_SPIRE = 42;
    vars.DUNGEON_TEMPORAL_PINNACLE = 43;
    vars.DUNGEON_MYSTIFYING_FOREST = 44;
    vars.DUNGEON_MYSTIFYING_FOREST_CLEARING = 45;
    vars.DUNGEON_BLIZZARD_ISLAND = 46;
    vars.DUNGEON_CREVICE_CAVE = 47;
    vars.DUNGEON_LOWER_CREVICE_CAVE = 48;
    vars.DUNGEON_CREVICE_CAVE_PIT = 49;
    vars.DUNGEON_SURROUNDED_SEA = 50;
    vars.DUNGEON_MIRACLE_SEA = 51;
    vars.DUNGEON_DEEP_MIRACLE_SEA = 52;
    vars.DUNGEON_MIRACLE_SEABED = 53;
    vars.DUNGEON_ICE_AEGIS_CAVE = 54;
    vars.DUNGEON_REGICE_CHAMBER = 55;
    vars.DUNGEON_ROCK_AEGIS_CAVE = 56;
    vars.DUNGEON_REGIROCK_CHAMBER = 57;
    vars.DUNGEON_STEEL_AEGIS_CAVE = 58;
    vars.DUNGEON_REGISTEEL_CHAMBER = 59;
    vars.DUNGEON_AEGIS_CAVE_PIT = 60;
    vars.DUNGEON_REGIGIGAS_CHAMBER = 61;
    vars.DUNGEON_MT_TRAVAIL = 62;
    vars.DUNGEON_THE_NIGHTMARE = 63;
    vars.DUNGEON_SPACIAL_RIFT = 64;
    vars.DUNGEON_DEEP_SPACIAL_RIFT = 65;
    vars.DUNGEON_SPACIAL_RIFT_BOTTOM = 66;
    vars.DUNGEON_DARK_CRATER = 67;
    vars.DUNGEON_DEEP_DARK_CRATER = 68;
    vars.DUNGEON_DARK_CRATER_PIT = 69;
    vars.DUNGEON_CONCEALED_RUINS = 70;
    vars.DUNGEON_DEEP_CONCEALED_RUINS = 71;
    vars.DUNGEON_MARINE_RESORT = 72;
    vars.DUNGEON_BOTTOMLESS_SEA = 73;
    vars.DUNGEON_BOTTOMLESS_SEA_DEPTHS = 74;
    vars.DUNGEON_SHIMMER_DESERT = 75;
    vars.DUNGEON_SHIMMER_DESERT_PIT = 76;
    vars.DUNGEON_MT_AVALANCHE = 77;
    vars.DUNGEON_MT_AVALANCHE_PEAK = 78;
    vars.DUNGEON_GIANT_VOLCANO = 79;
    vars.DUNGEON_GIANT_VOLCANO_PEAK = 80;
    vars.DUNGEON_WORLD_ABYSS = 81;
    vars.DUNGEON_WORLD_ABYSS_PIT = 82;
    vars.DUNGEON_SKY_STAIRWAY = 83;
    vars.DUNGEON_SKY_STAIRWAY_APEX = 84;
    vars.DUNGEON_MYSTERY_JUNGLE = 85;
    vars.DUNGEON_DEEP_MYSTERY_JUNGLE = 86;
    vars.DUNGEON_SERENITY_RIVER = 87;
    vars.DUNGEON_LANDSLIDE_CAVE = 88;
    vars.DUNGEON_LUSH_PRAIRIE = 89;
    vars.DUNGEON_TINY_MEADOW = 90;
    vars.DUNGEON_LABYRINTH_CAVE = 91;
    vars.DUNGEON_ORAN_FOREST = 92;
    vars.DUNGEON_LAKE_AFAR = 93;
    vars.DUNGEON_HAPPY_OUTLOOK = 94;
    vars.DUNGEON_MY_MISTRAL = 95;
    vars.DUNGEON_SHIMMER_HILL = 96;
    vars.DUNGEON_LOST_WILDERNESS = 97;
    vars.DUNGEON_MIDNIGHT_FOREST = 98;
    vars.DUNGEON_ZERO_ISLE_NORTH = 99;
    vars.DUNGEON_ZERO_ISLE_EAST = 100;
    vars.DUNGEON_ZERO_ISLE_WEST = 101;
    vars.DUNGEON_ZERO_ISLE_SOUTH = 102;
    vars.DUNGEON_ZERO_ISLE_CENTER = 103;
    vars.DUNGEON_DESTINY_TOWER = 104;
    vars.DUNGEON_DUMMY_0x69 = 105;
    vars.DUNGEON_DUMMY_0x6A = 106;
    vars.DUNGEON_OBLIVION_FOREST = 107;
    vars.DUNGEON_TREACHEROUS_WATERS = 108;
    vars.DUNGEON_SOUTHEASTERN_ISLANDS = 109;
    vars.DUNGEON_INFERNO_CAVE = 110;
    vars.DUNGEON_1ST_STATION_PASS = 111;
    vars.DUNGEON_2ND_STATION_PASS = 112;
    vars.DUNGEON_3RD_STATION_PASS = 113;
    vars.DUNGEON_4TH_STATION_PASS = 114;
    vars.DUNGEON_5TH_STATION_PASS = 115;
    vars.DUNGEON_6TH_STATION_PASS = 116;
    vars.DUNGEON_7TH_STATION_PASS = 117;
    vars.DUNGEON_8TH_STATION_PASS = 118;
    vars.DUNGEON_9TH_STATION_PASS = 119;
    vars.DUNGEON_SKY_PEAK_SUMMIT_PASS = 120;
    vars.DUNGEON_5TH_STATION_CLEARING = 121;
    vars.DUNGEON_SKY_PEAK_SUMMIT = 122;
    vars.DUNGEON_STAR_CAVE_SE1 = 123;
    vars.DUNGEON_DEEP_STAR_CAVE = 124;
    vars.DUNGEON_DEEP_STAR_CAVE_TEAM_ROGUE = 125;
    vars.DUNGEON_STAR_CAVE_DEPTHS = 126;
    vars.DUNGEON_STAR_CAVE_PIT = 127;
    vars.DUNGEON_MURKY_FOREST = 128;
    vars.DUNGEON_EASTERN_CAVE = 129;
    vars.DUNGEON_FORTUNE_RAVINE = 130;
    vars.DUNGEON_FORTUNE_RAVINE_DEPTHS = 131;
    vars.DUNGEON_FORTUNE_RAVINE_PIT = 132;
    vars.DUNGEON_BARREN_VALLEY = 133;
    vars.DUNGEON_DEEP_BARREN_VALLEY = 134;
    vars.DUNGEON_BARREN_VALLEY_CLEARING = 135;
    vars.DUNGEON_DARK_WASTELAND = 136;
    vars.DUNGEON_TEMPORAL_TOWER_SE5 = 137;
    vars.DUNGEON_TEMPORAL_SPIRE_SE5 = 138;
    vars.DUNGEON_DUSK_FOREST_SE5 = 139;
    vars.DUNGEON_BLACK_SWAMP = 140;
    vars.DUNGEON_SPACIAL_CLIFFS = 141;
    vars.DUNGEON_DARK_ICE_MOUNTAIN = 142;
    vars.DUNGEON_DARK_ICE_MOUNTAIN_PEAK = 143;
    vars.DUNGEON_DARK_ICE_MOUNTAIN_PINNACLE = 144;
    vars.DUNGEON_ICICLE_FOREST = 145;
    vars.DUNGEON_VAST_ICE_MOUNTAIN = 146;
    vars.DUNGEON_VAST_ICE_MOUNTAIN_PEAK = 147;
    vars.DUNGEON_VAST_ICE_MOUNTAIN_PINNACLE = 148;
    vars.DUNGEON_SOUTHERN_JUNGLE = 149;
    vars.DUNGEON_BOULDER_QUARRY = 150;
    vars.DUNGEON_DEEP_BOULDER_QUARRY = 151;
    vars.DUNGEON_BOULDER_QUARRY_CLEARING = 152;
    vars.DUNGEON_RIGHT_CAVE_PATH = 153;
    vars.DUNGEON_LEFT_CAVE_PATH = 154;
    vars.DUNGEON_LIMESTONE_CAVERN = 155;
    vars.DUNGEON_DEEP_LIMESTONE_CAVERN = 156;
    vars.DUNGEON_LIMESTONE_CAVERN_DEPTHS = 157;
    vars.DUNGEON_SPRING_CAVE = 158;
    vars.DUNGEON_UPPER_SPRING_CAVE_HAUNTER = 159;
    vars.DUNGEON_UPPER_SPRING_CAVE = 160;
    vars.DUNGEON_MIDDLE_SPRING_CAVE = 161;
    vars.DUNGEON_LOWER_SPRING_CAVE = 162;
    vars.DUNGEON_SPRING_CAVE_DEPTHS = 163;
    vars.DUNGEON_SPRING_CAVE_PIT = 164;
    vars.DUNGEON_LITTLE_PLAINS = 165;
    vars.DUNGEON_MT_CLEAR = 166;
    vars.DUNGEON_CHALLENGE_RIVER = 167;
    vars.DUNGEON_TRIAL_FOREST = 168;
    vars.DUNGEON_GUIDING_SEA = 169;
    vars.DUNGEON_HIDDEN_SHOPKEEPER_VILLAGE = 170;
    vars.DUNGEON_DUMMY_0xAB = 171;
    vars.DUNGEON_DUMMY_0xAC = 172;
    vars.DUNGEON_DUMMY_0xAD = 173;
    vars.DUNGEON_STAR_CAVE = 174;
    vars.DUNGEON_SHAYMIN_VILLAGE = 175;
    vars.DUNGEON_ARMALDOS_SHELTER = 176;
    vars.DUNGEON_LUMINOUS_SPRING = 177;
    vars.DUNGEON_HOT_SPRING = 178;
    vars.DUNGEON_RESCUE = 179;
    vars.DUNGEON_NORMAL_FLY_MAZE = 180;
    vars.DUNGEON_DARK_FIRE_MAZE = 181;
    vars.DUNGEON_ROCK_WATER_MAZE = 182;
    vars.DUNGEON_GRASS_MAZE = 183;
    vars.DUNGEON_ELEC_STEEL_MAZE = 184;
    vars.DUNGEON_ICE_GROUND_MAZE = 185;
    vars.DUNGEON_FIGHT_PSYCH_MAZE = 186;
    vars.DUNGEON_POISON_BUG_MAZE = 187;
    vars.DUNGEON_DRAGON_MAZE = 188;
    vars.DUNGEON_GHOST_MAZE = 189;
    vars.DUNGEON_EXPLORER_MAZE = 190;
    vars.DUNGEON_FINAL_MAZE = 191;
    vars.DUNGEON_DOJO_0xC0 = 192;
    vars.DUNGEON_DOJO_0xC1 = 193;
    vars.DUNGEON_DOJO_0xC2 = 194;
    vars.DUNGEON_DOJO_0xC3 = 195;
    vars.DUNGEON_DOJO_0xC4 = 196;
    vars.DUNGEON_DOJO_0xC5 = 197;
    vars.DUNGEON_DOJO_0xC6 = 198;
    vars.DUNGEON_DOJO_0xC7 = 199;
    vars.DUNGEON_DOJO_0xC8 = 200;
    vars.DUNGEON_DOJO_0xC9 = 201;
    vars.DUNGEON_DOJO_0xCA = 202;
    vars.DUNGEON_DOJO_0xCB = 203;
    vars.DUNGEON_DOJO_0xCC = 204;
    vars.DUNGEON_DOJO_0xCD = 205;
    vars.DUNGEON_DOJO_0xCE = 206;
    vars.DUNGEON_DOJO_0xCF = 207;
    vars.DUNGEON_DOJO_0xD0 = 208;
    vars.DUNGEON_DOJO_0xD1 = 209;
    vars.DUNGEON_DOJO_0xD2 = 210;
    vars.DUNGEON_DOJO_0xD3 = 211;
    vars.DUNGEON_RESCUE_DUNGEON = 212;
    vars.DUNGEON_BASE = 213;
    vars.DUNGEON_JOINED_AT_QUESTION_MARKS = 214; // Joined at ???
    vars.DUNGEON_BEACH = 215;
    vars.DUNGEON_JOINED_AT_UNKNOWN = 216;
    vars.DUNGEON_JOINED_AT_BIDOOF = 217;
    vars.DUNGEON_JOINED_AT_GROVYLE = 218;
    vars.DUNGEON_JOINED_AT_CELEBI = 219;
    vars.DUNGEON_JOINED_AT_CHATOT = 220;
    vars.DUNGEON_JOINED_AT_CRESSELIA = 221; // As a guest member during the story
    vars.DUNGEON_DUMMY_0xDE = 222;
    vars.DUNGEON_DUMMY_0xDF = 223;
    vars.DUNGEON_DUMMY_0xE0 = 224;
    vars.DUNGEON_DUMMY_0xE1 = 225;
    vars.DUNGEON_DUMMY_0xE2 = 226;
    vars.DUNGEON_DUMMY_0xE3 = 227;
    vars.DUNGEON_DUMMY_0xE4 = 228;
    vars.DUNGEON_DUMMY_0xE5 = 229;
    vars.DUNGEON_DUMMY_0xE6 = 230;
    vars.DUNGEON_DUMMY_0xE7 = 231;
    vars.DUNGEON_DUMMY_0xE8 = 232;
    vars.DUNGEON_DUMMY_0xE9 = 233;
    vars.DUNGEON_DUMMY_0xEA = 234;
    vars.DUNGEON_DUMMY_0xEB = 235;
    vars.DUNGEON_DUMMY_0xEC = 236;
    vars.DUNGEON_DUMMY_0xED = 237;
    vars.DUNGEON_DUMMY_0xEE = 238;
    vars.DUNGEON_DUMMY_0xEF = 239;
    vars.DUNGEON_DUMMY_0xF0 = 240;
    vars.DUNGEON_JOINED_AT_MANAPHY_RECRUITED = 241;      // When recruited
    vars.DUNGEON_JOINED_AT_CRESSELIA_RECRUITED = 242;    // When recruited
    vars.DUNGEON_JOINED_AT_SHAYMIN_LAND_RECRUITED = 243; // When recruited
    vars.DUNGEON_DUMMY_0xF4 = 244;
    vars.DUNGEON_DUMMY_0xF5 = 245;
    vars.DUNGEON_DUMMY_0xF6 = 246;
    vars.DUNGEON_TREASURE_TOWN_0xF7 = 247;
    vars.DUNGEON_TREASURE_TOWN_0xF8 = 248;
    vars.DUNGEON_OUTSIDE = 249;
    vars.DUNGEON_CLIENT = 250;
    vars.DUNGEON_DUMMY_0xFB = 251;
    vars.DUNGEON_DUMMY_0xFC = 252;
    vars.DUNGEON_DUMMY_0xFD = 253;
    vars.DUNGEON_DUMMY_0xFE = 254;
    vars.DUNGEON_DUMMY_0xFF = 255;

    vars.floorCounts = new Dictionary<int, int> {
        { vars.DUNGEON_BEACH_CAVE, 4},
        { vars.DUNGEON_BEACH_CAVE_PIT, 1},
        { vars.DUNGEON_DRENCHED_BLUFF, 6},
        { vars.DUNGEON_MT_BRISTLE, 9},
        { vars.DUNGEON_MT_BRISTLE_PEAK, 1},
        { vars.DUNGEON_WATERFALL_CAVE, 8},
        { vars.DUNGEON_APPLE_WOODS, 12},
        { vars.DUNGEON_CRAGGY_COAST, 9},
        { vars.DUNGEON_MT_HORN, 14},
        { vars.DUNGEON_FOGGY_FOREST, 11},
        { vars.DUNGEON_STEAM_CAVE, 8},
        { vars.DUNGEON_UPPER_STEAM_CAVE, 7},
        { vars.DUNGEON_STEAM_CAVE_PEAK, 1},
        { vars.DUNGEON_AMP_PLAINS, 10},
        { vars.DUNGEON_FAR_AMP_PLAINS, 9},
        { vars.DUNGEON_AMP_CLEARING, 1},
        { vars.DUNGEON_NORTHERN_DESERT, 15},
        { vars.DUNGEON_QUICKSAND_CAVE, 10},
        { vars.DUNGEON_QUICKSAND_PIT, 10},
        { vars.DUNGEON_UNDERGROUND_LAKE, 1},
        { vars.DUNGEON_CRYSTAL_CAVE, 11},
        { vars.DUNGEON_CRYSTAL_CROSSING, 13},
        { vars.DUNGEON_CRYSTAL_LAKE, 1},
        { vars.DUNGEON_CHASM_CAVE, 8},
        { vars.DUNGEON_DARK_HILL, 15},
        { vars.DUNGEON_SEALED_RUIN, 8},
        { vars.DUNGEON_DEEP_SEALED_RUIN, 6},
        { vars.DUNGEON_SEALED_RUIN_PIT, 1},
        { vars.DUNGEON_DUSK_FOREST, 8},
        { vars.DUNGEON_DEEP_DUSK_FOREST, 12},
        { vars.DUNGEON_TREESHROUD_FOREST, 20},
        { vars.DUNGEON_BRINE_CAVE, 9},
        { vars.DUNGEON_LOWER_BRINE_CAVE, 5},
        { vars.DUNGEON_BRINE_CAVE_PIT, 1},
        { vars.DUNGEON_HIDDEN_LAND, 15},
        { vars.DUNGEON_HIDDEN_HIGHLAND, 8},
        { vars.DUNGEON_OLD_RUINS, 1},
        { vars.DUNGEON_TEMPORAL_TOWER, 13},
        { vars.DUNGEON_TEMPORAL_SPIRE, 10},
        { vars.DUNGEON_TEMPORAL_PINNACLE, 1},
        { vars.DUNGEON_MYSTIFYING_FOREST, 13},
        { vars.DUNGEON_MYSTIFYING_FOREST_CLEARING, 1},
        { vars.DUNGEON_BLIZZARD_ISLAND, 20},
        { vars.DUNGEON_CREVICE_CAVE, 10},
        { vars.DUNGEON_LOWER_CREVICE_CAVE, 4},
        { vars.DUNGEON_CREVICE_CAVE_PIT, 1},
        { vars.DUNGEON_SURROUNDED_SEA, 20},
        { vars.DUNGEON_MIRACLE_SEA, 18},
        { vars.DUNGEON_DEEP_MIRACLE_SEA, 4},
        { vars.DUNGEON_MIRACLE_SEABED, 1},
        { vars.DUNGEON_REGICE_CHAMBER, 1},
        { vars.DUNGEON_REGIROCK_CHAMBER, 1},
        { vars.DUNGEON_REGISTEEL_CHAMBER, 1},
        { vars.DUNGEON_AEGIS_CAVE_PIT, 5},
        { vars.DUNGEON_REGIGIGAS_CHAMBER, 1},
        { vars.DUNGEON_MT_TRAVAIL, 19},
        { vars.DUNGEON_THE_NIGHTMARE, 17},
        { vars.DUNGEON_SPACIAL_RIFT, 15},
        { vars.DUNGEON_DEEP_SPACIAL_RIFT, 9},
        { vars.DUNGEON_SPACIAL_RIFT_BOTTOM, 1},
        { vars.DUNGEON_DARK_CRATER, 15},
        { vars.DUNGEON_DEEP_DARK_CRATER, 14},
        { vars.DUNGEON_DARK_CRATER_PIT, 1},
        { vars.DUNGEON_1ST_STATION_PASS, 3},
        { vars.DUNGEON_2ND_STATION_PASS, 3},
        { vars.DUNGEON_3RD_STATION_PASS, 3},
        { vars.DUNGEON_4TH_STATION_PASS, 3},
        { vars.DUNGEON_5TH_STATION_PASS, 4},
        { vars.DUNGEON_6TH_STATION_PASS, 4},
        { vars.DUNGEON_7TH_STATION_PASS, 4},
        { vars.DUNGEON_8TH_STATION_PASS, 4},
        { vars.DUNGEON_9TH_STATION_PASS, 5},
        { vars.DUNGEON_SKY_PEAK_SUMMIT_PASS, 5},
        { vars.DUNGEON_5TH_STATION_CLEARING, 1},
        { vars.DUNGEON_SKY_PEAK_SUMMIT, 1},
    };

    settings.Add("pquiz", false, "Personality Quiz");
    settings.Add(vars.DUNGEON_BEACH_CAVE.ToString(), true, "Beach Cave");
    settings.Add(vars.DUNGEON_BEACH_CAVE_PIT.ToString(), true, "Koffing & Zubat");
    settings.Add(vars.DUNGEON_DRENCHED_BLUFF.ToString(), true, "Drenched Bluff");
    settings.Add(vars.DUNGEON_MT_BRISTLE.ToString(), true, "Mt. Bristle");
    settings.Add(vars.DUNGEON_MT_BRISTLE_PEAK.ToString(), true, "Drowzee");
    settings.Add("jd01", true, "Jobs Day 1 (3 Missions)");
    settings.Add("sd01", true, "Sentry Duty 1");
    settings.Add("jd02", true, "Jobs Day 2 (2 Missions)");
    settings.Add(vars.DUNGEON_WATERFALL_CAVE.ToString(), true, "Waterfall Cave");
    settings.Add("jd03", true, "Jobs Day 3 (1 Mission)");
    settings.Add("sd02", true, "Sentry Duty 2");
    settings.Add("jd04", true, "Jobs Day 4 (0 Missions)");
    settings.Add(vars.DUNGEON_APPLE_WOODS.ToString(), true, "Apple Woods");
    settings.Add("jd05", true, "Jobs Day 5 (1 Mission)");
    settings.Add("sd03", true, "Sentry Duty 3");
    settings.Add("jd06", true, "Jobs Day 6 (1 Mission)");
    settings.Add(vars.DUNGEON_CRAGGY_COAST.ToString(), true, "Craggy Coast");
    settings.Add(vars.DUNGEON_MT_HORN.ToString(), true, "Mt. Horn");
    settings.Add(vars.DUNGEON_FOGGY_FOREST.ToString(), true, "Foggy Forest");
    settings.Add(vars.DUNGEON_STEAM_CAVE.ToString(), true, "Steam Cave");
    settings.Add(vars.DUNGEON_UPPER_STEAM_CAVE.ToString(), true, "Upper Steam Cave");
    settings.Add(vars.DUNGEON_STEAM_CAVE_PEAK.ToString(), true, "Groudon");
    settings.Add("jd07", true, "Jobs Day 7 (0 Missions)");
    settings.Add("jd08", true, "Jobs Day 8 (0 Missions)");
    settings.Add(vars.DUNGEON_AMP_PLAINS.ToString(), true, "Amp Plains");
    settings.Add(vars.DUNGEON_FAR_AMP_PLAINS.ToString(), true, "Far Amp Plains");
    settings.Add(vars.DUNGEON_AMP_CLEARING.ToString(), true, "Manectric & Electikes");
    settings.Add(vars.DUNGEON_NORTHERN_DESERT.ToString(), true, "Northern Desert");
    settings.Add(vars.DUNGEON_QUICKSAND_CAVE.ToString(), true, "Quicksand Cave");
    settings.Add(vars.DUNGEON_QUICKSAND_PIT.ToString(), true, "Quicksand Pit");
    settings.Add(vars.DUNGEON_UNDERGROUND_LAKE.ToString(), true, "Mesprit");
    settings.Add(vars.DUNGEON_CRYSTAL_CAVE.ToString(), true, "Crystal Cave");
    settings.Add(vars.DUNGEON_CRYSTAL_CROSSING.ToString(), true, "Crystal Crossing");
    settings.Add("jd09", true, "Jobs Day 9 (0 Missions)");
    settings.Add("jd10", true, "Jobs Day 10 (0 Missions)");
    settings.Add(vars.DUNGEON_CHASM_CAVE.ToString(), true, "Chasm Cave");
    settings.Add(vars.DUNGEON_DARK_HILL.ToString(), true, "Dark Hill");
    settings.Add(vars.DUNGEON_SEALED_RUIN.ToString(), true, "Sealed Ruin");
    settings.Add(vars.DUNGEON_DEEP_SEALED_RUIN.ToString(), true, "Deep Sealed Ruin");
    settings.Add(vars.DUNGEON_SEALED_RUIN_PIT.ToString(), true, "Spiritomb");
    settings.Add(vars.DUNGEON_DUSK_FOREST.ToString(), true, "Dusk Forest");
    settings.Add(vars.DUNGEON_DEEP_DUSK_FOREST.ToString(), true, "Deep Dusk Forest");
    settings.Add(vars.DUNGEON_TREESHROUD_FOREST.ToString(), true, "Treeshroud Forest");
    settings.Add(vars.DUNGEON_WATERFALL_CAVE.ToString() + "_2", true, "Waterfall Cave 2");
    settings.Add(vars.DUNGEON_BRINE_CAVE.ToString(), true, "Brine Cave");
    settings.Add(vars.DUNGEON_LOWER_BRINE_CAVE.ToString(), true, "Lower Brine Cave");
    settings.Add(vars.DUNGEON_BRINE_CAVE_PIT.ToString(), true, "Kabutops & Omastars");
    settings.Add(vars.DUNGEON_HIDDEN_LAND.ToString(), true, "Hidden Land");
    settings.Add(vars.DUNGEON_HIDDEN_HIGHLAND.ToString(), true, "Hidden Highland");
    settings.Add(vars.DUNGEON_OLD_RUINS.ToString(), true, "Dusknoir & Sableyes");
    settings.Add(vars.DUNGEON_TEMPORAL_TOWER.ToString(), true, "Temporal Tower");
    settings.Add(vars.DUNGEON_TEMPORAL_SPIRE.ToString(), true, "Temporal Spire");
    settings.Add(vars.DUNGEON_TEMPORAL_PINNACLE.ToString(), false, "Primal Dialga");
    settings.Add("vet", false, "Any% Vanilla English End Timing");
    settings.Add("set", true, "Any% Speedrun Mod End Timing");
    settings.Add(vars.DUNGEON_MYSTIFYING_FOREST.ToString(), true, "Mystifying Forest");
    settings.Add(vars.DUNGEON_MYSTIFYING_FOREST_CLEARING.ToString(), true, "Guild");
    settings.Add("jd11", true, "Jobs Day 11 (0 Missions)");
    settings.Add("jd12", true, "Jobs Day 12 (1 Mission)");
    settings.Add(vars.DUNGEON_1ST_STATION_PASS.ToString(), true, "Sky Peak 1st Station Pass");
    settings.Add(vars.DUNGEON_2ND_STATION_PASS.ToString(), true, "Sky Peak 2nd Station Pass");
    settings.Add(vars.DUNGEON_3RD_STATION_PASS.ToString(), true, "Sky Peak 3rd Station Pass");
    settings.Add(vars.DUNGEON_4TH_STATION_PASS.ToString(), true, "Sky Peak 4th Station Pass");
    settings.Add(vars.DUNGEON_5TH_STATION_PASS.ToString(), true, "Sky Peak 5th Station Pass");
    settings.Add(vars.DUNGEON_6TH_STATION_PASS.ToString(), true, "Sky Peak 6th Station Pass");
    settings.Add(vars.DUNGEON_7TH_STATION_PASS.ToString(), true, "Sky Peak 7th Station Pass");
    settings.Add(vars.DUNGEON_8TH_STATION_PASS.ToString(), true, "Sky Peak 8th Station Pass");
    settings.Add(vars.DUNGEON_9TH_STATION_PASS.ToString(), true, "Sky Peak 9th Station Pass");
    settings.Add(vars.DUNGEON_SKY_PEAK_SUMMIT_PASS.ToString(), true, "Sky Peak Summit Pass");
    settings.Add(vars.DUNGEON_SKY_PEAK_SUMMIT.ToString(), true, "Muks & Grimers");
    settings.Add("jd13", true, "Jobs Day 13 (0 Missions)");
    settings.Add(vars.DUNGEON_BLIZZARD_ISLAND.ToString(), true, "Blizzard Island");
    settings.Add(vars.DUNGEON_CREVICE_CAVE.ToString(), true, "Crevice Cave");
    settings.Add(vars.DUNGEON_LOWER_CREVICE_CAVE.ToString(), true, "Lower Crevice Cave");
    settings.Add(vars.DUNGEON_CREVICE_CAVE_PIT.ToString(), true, "Froslass");
    settings.Add("jd14", true, "Jobs Day 14 (1 Mission)");
    settings.Add(vars.DUNGEON_SURROUNDED_SEA.ToString(), true, "Surrounded Sea");
    settings.Add(vars.DUNGEON_MIRACLE_SEA.ToString(), true, "Miracle Sea");
    settings.Add(vars.DUNGEON_DEEP_MIRACLE_SEA.ToString(), true, "Deep Miracle Sea");
    settings.Add(vars.DUNGEON_MIRACLE_SEABED.ToString(), true, "Gyarados");
    settings.Add("jd15", true, "Jobs Day 15 (0 Missions)");
    settings.Add("jd16", true, "Jobs Day 16 (2 Missions)");
    settings.Add(vars.DUNGEON_REGICE_CHAMBER.ToString(), true, "Regice");
    settings.Add(vars.DUNGEON_REGIROCK_CHAMBER.ToString(), true, "Regirock");
    settings.Add(vars.DUNGEON_REGISTEEL_CHAMBER.ToString(), true, "Registeel");
    settings.Add(vars.DUNGEON_AEGIS_CAVE_PIT.ToString(), true, "Aegis Cave Pit");
    settings.Add(vars.DUNGEON_REGIGIGAS_CHAMBER.ToString(), true, "Regigigas");
    settings.Add("jd17", true, "Jobs Day 17 (1 Mission)");
    settings.Add("jd18", true, "Jobs Day 18 (0 Missions)");
    settings.Add("jd19", true, "Jobs Day 19 (0 Missions)");
    settings.Add(vars.DUNGEON_MT_TRAVAIL.ToString(), true, "Mt. Travail");
    settings.Add(vars.DUNGEON_THE_NIGHTMARE.ToString(), true, "The Nightmare");
    settings.Add(vars.DUNGEON_SPACIAL_RIFT.ToString(), true, "Spacial Rift");
    settings.Add(vars.DUNGEON_DEEP_SPACIAL_RIFT.ToString(), true, "Deep Spacial Rift");
    settings.Add(vars.DUNGEON_SPACIAL_RIFT_BOTTOM.ToString(), true, "Palkia");
    settings.Add(vars.DUNGEON_DARK_CRATER.ToString(), true, "Dark Crater");
    settings.Add(vars.DUNGEON_DEEP_DARK_CRATER.ToString(), true, "Deep Dark Crater");
    settings.Add(vars.DUNGEON_DARK_CRATER_PIT.ToString(), true, "Darkrai & Co.");
    settings.Add("dset", true, "Beat Darkrai Speedrun Mod End Timing");

    vars.completedSplits = new HashSet<string>();

    vars.detectGameVersion = (Action)(() => {
        if (vars.versionDetection.Current == 0xAF) {
            // EU
            print("Detected EU version.");
            vars.PLAY_TIME_SECONDS = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ABFD4)));
            vars.PLAY_TIME_FRAME_COUNTER = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ABFD8)));
            vars.SCENARIO_MAIN_FLAG_MAIN = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ABAA8)));
            vars.SCENARIO_MAIN_FLAG_SUB = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ABAA9)));
            vars.REQUEST_CLEAR_COUNT = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ABADB)));
            vars.brightness = new MemoryWatcher<short>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2A40E4)));
            vars.magic_number = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2A3670)));
            vars.overlay1_start = new MemoryWatcher<short>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x329D40)));
            vars.dungeon_ptr = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x354138))); // pointer to the dungeon struct. this struct is usually at a fixed location, but a soft-reset seems to potentially shuffle its location...
            vars.current_script_id = new MemoryWatcher<long>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x325ACA)));
        }
        else if (vars.versionDetection.Current == 0x1A) {
            // NA
            print("Detected NA version.");
            vars.PLAY_TIME_SECONDS = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AB694)));
            vars.PLAY_TIME_FRAME_COUNTER = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AB698)));
            vars.SCENARIO_MAIN_FLAG_MAIN = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AB168)));
            vars.SCENARIO_MAIN_FLAG_SUB = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AB169)));
            vars.REQUEST_CLEAR_COUNT = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AB19B)));
            vars.brightness = new MemoryWatcher<short>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2A37A4)));
            vars.magic_number = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2A2E10))); // maybe it's 0x2A2EB0
            vars.overlay1_start = new MemoryWatcher<short>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x329520)));
            vars.dungeon_ptr = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x353538))); // pointer to the dungeon struct. this struct is usually at a fixed location, but a soft-reset seems to potentially shuffle its location...
            vars.current_script_id = new MemoryWatcher<long>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x324F8A)));
        }
        else if (vars.versionDetection.Current == 0xB5) {
            // JP
            print("Detected JP version.");
            vars.PLAY_TIME_SECONDS = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ACE50)));
            vars.PLAY_TIME_FRAME_COUNTER = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2ACE54)));
            vars.SCENARIO_MAIN_FLAG_MAIN = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AC924)));
            vars.SCENARIO_MAIN_FLAG_SUB = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AC925)));
            vars.REQUEST_CLEAR_COUNT = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2AC957)));
            vars.brightness = new MemoryWatcher<short>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2A5008))); // maybe it's 0x2A5004
            vars.magic_number = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x2A4590))); // maybe it's 0x2A4710
            vars.overlay1_start = new MemoryWatcher<short>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x32ACC0)));
            vars.dungeon_ptr = new MemoryWatcher<int>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x3547B8))); // pointer to the dungeon struct. this struct is usually at a fixed location, but a soft-reset seems to potentially shuffle its location...
            vars.current_script_id = new MemoryWatcher<long>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x3264EA)));
        }
        else {
            print("Unknown game version.");
            // throwing an exception lets it retry the frame after
            throw new Exception("[PMD EoS Autosplitter] Unknown game version.");
        }

        vars.currentVersion = vars.versionDetection.Current;
    });
}

init
{
    print("[PMD EoS Autosplitter] Initializing.");

    var exe = modules.First();

    vars.RAMAddress = IntPtr.Zero;

    string normalizedProcessName = memory.ProcessName.ToLower();

    if (normalizedProcessName.Contains("melonds")) {
        
        int[] offsets = null;

        if (exe.ModuleMemorySize == 0xA9A4000) {
            print("Detected MelonDS v1.1, applying pointer path...");
                offsets = new int[] {
                    0x2031DE0,
                    0xA0,
                    0x405438,
                    0x0
                };
        }
        else if (exe.ModuleMemorySize == 0xA5E8000) {
            print("Detected MelonDS v1.0 RC, applying pointer path...");
                offsets = new int[] {
                    0x257F080,
                    0xA0,
                    0x405408,
                    0x0
                };
        }
        else {
            print("Detected MelonDS, but version is unknown.");
            // throwing an exception lets it retry the frame after
            throw new Exception("[PMD EoS Autosplitter] Detected MelonDS, but version is unknown.");
        }
        
        if (offsets != null) {
            IntPtr ptr = IntPtr.Add(exe.BaseAddress, offsets[0]);

            for (int i = 1; i < offsets.Length; i++)
            {
                ptr = IntPtr.Add(game.ReadPointer(ptr), offsets[i]);
            }

            vars.RAMAddress = ptr;
        }
        
    }
    else if (normalizedProcessName.Contains("desmume_0.9.13")) {
        print("Detected DeSmuME v0.9.13, applying pointer path...");
        vars.RAMAddress = IntPtr.Add(exe.BaseAddress, 0xA915400);
    }
    else if (normalizedProcessName.Contains("desmume_0.9.11")) {
        print("Detected DeSmuME v0.9.11, applying pointer path...");
        vars.RAMAddress = IntPtr.Add(exe.BaseAddress, 0x5411250);
    }

    if (vars.RAMAddress == IntPtr.Zero) {
        print("Could not find RAM Address!");
        throw new Exception("[PMD EoS Autosplitter] Could not find RAM address");
    }

    print("RAM Address found at " + vars.RAMAddress.ToString("X") + ".");

    vars.versionDetection = new MemoryWatcher<byte>(new DeepPointer(IntPtr.Add(vars.RAMAddress, 0x0E)));
    vars.versionDetection.Update(game);
    vars.currentVersion = 0x00;

    vars.detectGameVersion();
}

split
{
    bool hasFullyFadedOut = vars.brightness.Current < -0xFF && vars.brightness.Current < vars.brightness.Old;
    
    // dungeon splitting
    if (vars.dungeon_ptr.Current != 0x00000000) {
        byte current_dungeon_id = vars.current_dungeon_id;
        string current_dungeon_split = "";

        if (vars.SCENARIO_MAIN_FLAG_MAIN.Current == 22 && current_dungeon_id == vars.DUNGEON_WATERFALL_CAVE) {
            current_dungeon_split = current_dungeon_id.ToString() + "_2";
        }
        else {
            current_dungeon_split = current_dungeon_id.ToString();
        }

        if (settings[current_dungeon_split] && vars.current_floor >= vars.floorCounts[current_dungeon_id] && vars.is_clearing_floor && hasFullyFadedOut && vars.current_floor_old == vars.current_floor && vars.completedSplits.Add(current_dungeon_split)) {
            return true;
        }
    }

    // personality quiz, 0x313031304131304D = M01A0101, 0x383032304131304D = M01A0208
    if ((vars.current_script_id.Current == 0x313031304131304D || vars.current_script_id.Current == 0x383032304131304D) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 2) {
        if (settings["pquiz"] && vars.completedSplits.Add("pquiz")) {
            return true;
        }
    }

    // any% vanilla end timing
    if (vars.current_script_id.Current == 0x353136304136324D) { // M26A0615
        if (settings["vet"] && hasFullyFadedOut && vars.completedSplits.Add("vet")) {
            return true;
        }
    }

    // any% speedrun mod end timing
    if (vars.current_script_id.Current == 0x363038304136324D) { // M26A0806
        if (settings["set"] && vars.completedSplits.Add("set")) {
            return true;
        }
    }

    // beat darkrai speedrun mod end timing. no current script id for this one because i was a fool and i placed the save in unionall :)
    if (vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 92) { // M26A0806
        if (settings["dset"] && vars.completedSplits.Add("dset")) {
            return true;
        }
    }

    // We Enjoy Typing!
    
    // not every post-job-day sequence updates SCENARIO_MAIN_FLAGs, so we're delaying the split until the next script is loaded in order to normalize split timing
    if (vars.current_script_id.Current == 0x3130323041303043) { // C00A0201
        // jobs day 1, 0x3130323041303043 = C00A0201
        if (vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 5 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 0) {
            if (settings["jd01"] && vars.completedSplits.Add("jd01")) {
                return true;
            }
        }
        // sentry duty 1, 0x3130363041303043 = C00A0601, 0x313033304134304D = M04A0301
        if ((vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 5 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 5) || (vars.current_script_id.Current == 0x313033304134304D) || (vars.current_script_id.Current == 0x3130363041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 5 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 3)) {           
            if (settings["sd01"] && vars.completedSplits.Add("sd01")) {
                return true;
            }
        }
        // jobs day 2 
        else if (vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 6 ) {
            if (settings["jd02"] && vars.completedSplits.Add("jd02")) {
                return true;
            }
        }
        // jobs day 3 
        else if (vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 7 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 3 && vars.REQUEST_CLEAR_COUNT.Current >= 1 ) { // no SCENARIO_MAIN_FLAG changes occur here, instead it looks up REQUEST_CLEAR_COUNT and sets SCENARIO_MAIN_BIT_FLAG[10] if it's high enough.
            if (settings["jd03"] && vars.completedSplits.Add("jd03")) {
                return true;
            }
        }
        // sentry duty 2
        else if (vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 7 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 5) {
            if (settings["sd02"] && vars.completedSplits.Add("sd02")) {
                return true;
            }
        }
        // jobs day 4, 0x313031304137304D = M07A0101
        else if ((vars.current_script_id.Current == 0x3130323041303043 || vars.current_script_id.Current == 0x313031304137304D) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 8 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 0 ) {
            if (settings["jd04"] && vars.completedSplits.Add("jd04")) {
                return true;
            }
        }
        // jobs day 5
        else if ((vars.current_script_id.Current == 0x3130323041303043 || vars.current_script_id.Current == 0x3130363041303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 8 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 7 ) {
            if (settings["jd05"] && vars.completedSplits.Add("jd05")) {
                return true;
            }
        }
        // sentry duty 3
        else if ((vars.current_script_id.Current == 0x3130323041303043 || vars.current_script_id.Current == 0x3130363041303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 8 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 8) {
            if (settings["sd03"] && vars.completedSplits.Add("sd03")) {
                return true;
            }
        }
        // jobs day 6, 0x313031304138304D = M08A0101
        else if ((vars.current_script_id.Current == 0x3130323041303043 || vars.current_script_id.Current == 0x313031304138304D) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 9 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 0 ) {
            if (settings["jd06"] && vars.completedSplits.Add("jd06")) {
                return true;
            }
        }
        // jobs day 7
        else if ((vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 12 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 2) || (vars.current_script_id.Current == 0x3130363041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 12 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 1)) {
            if (settings["jd07"] && vars.completedSplits.Add("jd07")) {
                return true;
            }
        }
        // jobs day 8, 0x313036304131314D = M11A0601
        else if ((vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 13 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 0) || vars.current_script_id.Current == 0x313036304131314D) {
            if (settings["jd08"] && vars.completedSplits.Add("jd08")) {
                return true;
            }
        }
        // jobs day 9
        else if ((vars.current_script_id.Current == 0x3130363041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 17 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 1) || (vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 17 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 2)) {
            if (settings["jd09"] && vars.completedSplits.Add("jd09")) {
                return true;
            }
        }
        // jobs day 10
        else if ((vars.current_script_id.Current == 0x3130363041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 17 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 3) || (vars.current_script_id.Current == 0x3130323041303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 17 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 4)) {
            if (settings["jd10"] && vars.completedSplits.Add("jd10")) {
                return true;
            }
        }
        // jobs day 11, 0x313036304131314D = C00A1001, 0x3130313141303043 = C00A1101
        else if ((vars.current_script_id.Current == 0x3130303141303043 || vars.current_script_id.Current == 0x3130313141303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 6) {
            if (settings["jd11"] && vars.completedSplits.Add("jd11")) {
                return true;
            }
        }
        // jobs day 12
        else if ((vars.current_script_id.Current == 0x3130303141303043 || vars.current_script_id.Current == 0x3130313141303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 7) {
            if (settings["jd12"] && vars.completedSplits.Add("jd12")) {
                return true;
            }
        }
        // jobs day 13
        else if ((vars.current_script_id.Current == 0x3130303141303043 || vars.current_script_id.Current == 0x3130313141303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 33) {
            if (settings["jd13"] && vars.completedSplits.Add("jd13")) {
                return true;
            }
        }
        // jobs day 14
        else if ((vars.current_script_id.Current == 0x3130303141303043 || vars.current_script_id.Current == 0x3130313141303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 40) {
            if (settings["jd14"] && vars.completedSplits.Add("jd14")) {
                return true;
            }
        }
        // jobs day 15
        else if ((vars.current_script_id.Current == 0x3130303141303043 || vars.current_script_id.Current == 0x3130313141303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 53) {
            if (settings["jd15"] && vars.completedSplits.Add("jd15")) {
                return true;
            }
        }
        // jobs day 16
        else if ((vars.current_script_id.Current == 0x3130303141303043 || vars.current_script_id.Current == 0x3130313141303043) && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 54) {
            if (settings["jd16"] && vars.completedSplits.Add("jd16")) {
                return true;
            }
        }
        // jobs day 17, = 0x3730313050303253 = S20P0107
        else if ((vars.current_script_id.Current == 0x3130303141303043 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 68) || (vars.current_script_id.Current == 0x3730313050303253 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 69)) {
            if (settings["jd17"] && vars.completedSplits.Add("jd17")) {
                return true;
            }
        }
        // jobs day 18, = 0x3130323050303253 = S20P0201, 0x3430323050303253 = S20P0204
        else if ((vars.current_script_id.Current == 0x3130323050303253 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 70) || (vars.current_script_id.Current == 0x3430323050303253 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 70)) {
            if (settings["jd18"] && vars.completedSplits.Add("jd18")) {
                return true;
            }
        }
        // jobs day 19, = 0x3130333050303253 = S20P0301, 0x3530333050303253 = S20P0305
        else if ((vars.current_script_id.Current == 0x3130333050303253 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 71) || (vars.current_script_id.Current == 0x3530333050303253 && vars.SCENARIO_MAIN_FLAG_MAIN.Current == 29 && vars.SCENARIO_MAIN_FLAG_SUB.Current == 71)) {
            if (settings["jd19"] && vars.completedSplits.Add("jd19")) {
                return true;
            }
        }
    }
}

start
{
    if (vars.overlay1_start.Current == 0x0004 && vars.magic_number.Current == 0x00000001 && vars.brightness.Current < 0 && vars.PLAY_TIME_SECONDS.Current == 0 && vars.PLAY_TIME_FRAME_COUNTER.Current == 0) {
        return true;
    }
}

onStart
{
    vars.second_to_display = vars.PLAY_TIME_SECONDS.Current;
    vars.frames_to_display = vars.PLAY_TIME_FRAME_COUNTER.Current;
    timer.IsGameTimePaused = true;
    vars.completedSplits.Clear();
}


reset
{
    if (vars.overlay1_start.Current == 0x0004 && vars.brightness.Current == 0 && vars.PLAY_TIME_SECONDS.Current == 0 && vars.PLAY_TIME_FRAME_COUNTER.Current == 0) {
        return true;
    }
}

update
{
    // TODO: rename these wild naming conventions
    vars.PLAY_TIME_SECONDS.Update(game);
    vars.PLAY_TIME_FRAME_COUNTER.Update(game);
    vars.SCENARIO_MAIN_FLAG_MAIN.Update(game);
    vars.SCENARIO_MAIN_FLAG_SUB.Update(game);
    vars.REQUEST_CLEAR_COUNT.Update(game);
    vars.brightness.Update(game);
    vars.magic_number.Update(game);
    vars.overlay1_start.Update(game);
    vars.dungeon_ptr.Update(game);
    vars.current_script_id.Update(game);
    vars.versionDetection.Update(game);

    if (vars.dungeon_ptr.Current != 0x00000000) {
        int relative_dungeon_ptr = vars.dungeon_ptr.Current - 0x2000000;
        IntPtr current_dungeon_id_addr = IntPtr.Add(vars.RAMAddress, relative_dungeon_ptr + 0x748);
        IntPtr current_floor_addr = IntPtr.Add(vars.RAMAddress, relative_dungeon_ptr + 0x749);
        IntPtr is_clearing_floor_addr = IntPtr.Add(vars.RAMAddress, relative_dungeon_ptr + 0x6);
        vars.is_clearing_floor = game.ReadValue<bool>(is_clearing_floor_addr);
        vars.current_dungeon_id = game.ReadValue<byte>(current_dungeon_id_addr);
        vars.current_floor_old = vars.current_floor;
        vars.current_floor = game.ReadValue<byte>(current_floor_addr);
    }

    if (vars.currentVersion != vars.versionDetection.Current) {
        print("[PMD EoS Autosplitter] Game version changed. Re-running the version detection.");
        try {
            vars.detectGameVersion();
        }
        catch {
            // catching this exception, even without handling it, allows continued retrying until there is a known game version
        }
    }
}

gameTime
{
    if (vars.PLAY_TIME_SECONDS.Current != 0 || vars.PLAY_TIME_FRAME_COUNTER.Current != 0) {
        vars.second_to_display = vars.PLAY_TIME_SECONDS.Current;
        vars.frames_to_display = vars.PLAY_TIME_FRAME_COUNTER.Current;
    }
    return TimeSpan.FromSeconds(vars.second_to_display) + TimeSpan.FromMilliseconds(vars.millisecond_lookup[vars.frames_to_display]);
}

isLoading
{
    return true;
}
