#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/Os/Error.h"

static char s_emptyError[] = "";

char* OsGetLastErrorStr() {
    char* text = strdup(strerror(errno));
    return text ? text : s_emptyError;
}

void OsFreeLastErrorStr(char* str) {
    if (str != s_emptyError) {
        free(str);
    }
}
