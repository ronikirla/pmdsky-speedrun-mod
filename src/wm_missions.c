// This file can be used to build a ROM with hardcoded missions in runs.
// Enable by removing respective comments in patch.asm

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"

struct mission missions[] = {
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_TREASURE_MEMO},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 1,
    .description_id = 0,
    .unique_map_id = 81,
    .client = {.val = MONSTER_TURTWIG},
    .target = {.val = MONSTER_TURTWIG},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_NOTHING},
    .reward_type = {.val = MISSION_REWARD_ITEM_AND_MORE},
    .item_reward = {.val = ITEM_APPLE},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_TREASURE_MEMO},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 2,
    .description_id = 0,
    .unique_map_id = 81,
    .client = {.val = MONSTER_TURTWIG},
    .target = {.val = MONSTER_TURTWIG},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_NOTHING},
    .reward_type = {.val = MISSION_REWARD_ITEM_AND_MORE},
    .item_reward = {.val = ITEM_APPLE},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_TREASURE_MEMO},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 3,
    .description_id = 0,
    .unique_map_id = 81,
    .client = {.val = MONSTER_TURTWIG},
    .target = {.val = MONSTER_TURTWIG},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_NOTHING},
    .reward_type = {.val = MISSION_REWARD_ITEM_AND_MORE},
    .item_reward = {.val = ITEM_APPLE},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_PROSPECT_WITH_CLIENT},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 3,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_TRAPINCH},
    .target = {.val = MONSTER_TRAPINCH},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_MOBILE_SCARF},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_GOLDEN_SEED},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_FIND_ITEM},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 4,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_MUK},
    .target = {.val = MONSTER_MUK},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_CALCIUM},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_PURE_SEED},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_FIND_ITEM},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 0,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_MUK},
    .target = {.val = MONSTER_MUK},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_DEF_SCARF},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_ESCAPE_ORB},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_PROSPECT_WITH_CLIENT},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 4,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_TRAPINCH},
    .target = {.val = MONSTER_TRAPINCH},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_TM_FRUSTRATION},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_WEATHER_BAND},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_FIND_ITEM},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 0,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_MUK},
    .target = {.val = MONSTER_MUK},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_GOLDEN_MASK},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_MOBILE_SCARF},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },

  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_TREASURE_MEMO},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 4,
    .description_id = 0,
    .unique_map_id = 81,
    .client = {.val = MONSTER_TURTWIG},
    .target = {.val = MONSTER_TURTWIG},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_NOTHING},
    .reward_type = {.val = MISSION_REWARD_ITEM_AND_MORE},
    .item_reward = {.val = ITEM_APPLE},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_PROSPECT_WITH_CLIENT},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 4,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_TRAPINCH},
    .target = {.val = MONSTER_TRAPINCH},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_GOLDEN_SEED},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_ESCAPE_ORB},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_PROSPECT_WITH_CLIENT},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 4,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_TRAPINCH},
    .target = {.val = MONSTER_TRAPINCH},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_WONDER_GUMMI},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_FIGHT_GEM},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
  {
    .status = {.val = MISSION_STATUS_SUSPENDED},
    .type = {.val = MISSION_FIND_ITEM},
    .subtype = {.none = 0},
    .dungeon_id = {.val = DUNGEON_BEACH_CAVE},
    .floor = 0,
    .description_id = 0,
    .unique_map_id = 0,
    .client = {.val = MONSTER_MUK},
    .target = {.val = MONSTER_MUK},
    .outlaw_backup_species = {.val = MONSTER_NONE},
    .item_wanted = {.val = ITEM_GOLDEN_MASK},
    .reward_type = {.val = MISSION_REWARD_ITEM},
    .item_reward = {.val = ITEM_WHITE_SILK},
    .restriction_type = {.val = MISSION_RESTRICTION_NONE}
  },
};

__attribute__((used)) enum mission_generation_result GenerateCustomMission(int32_t i, struct mission* mission_struct) {
  if (i < sizeof(missions) / sizeof(struct mission)) {
    memcpy(mission_struct, &missions[i], sizeof(struct mission));
  }
  return MISSION_GENERATION_SUCCESS;
}