// This file contains optimizations to make the game run faster

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"

__attribute__((used)) void SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser(
    struct entity* entity, int message_id
) {
    SubstitutePlaceholderStringTags(0, entity, 0);
    LogMessageByIdWithPopupCheckUser(entity, message_id);
}