// This file contains optimizations to make the game run faster

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"

__attribute__((used)) void SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser(
    int string_id, struct entity* entity, undefined4 param_3, int message_id
) {
    SubstitutePlaceholderStringTags(string_id, entity, param_3);
    LogMessageByIdWithPopupCheckUser(entity, message_id);
}