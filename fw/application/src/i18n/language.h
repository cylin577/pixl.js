#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "language_list.h"
#include "string_id.h"

#define _T(x) getLangString(_L_##x)

typedef enum {
#define LANGUAGE_ENUM_ENTRY(suffix, file, desc) LANGUAGE_##suffix,
    LANGUAGE_LIST(LANGUAGE_ENUM_ENTRY)
#undef LANGUAGE_ENUM_ENTRY
    LANGUAGE_COUNT
} Language;

#define LANGUAGE_EXTERN_ENTRY(suffix, file, desc) extern const char* lang_##file[_L_COUNT];
LANGUAGE_LIST(LANGUAGE_EXTERN_ENTRY)
#undef LANGUAGE_EXTERN_ENTRY

// 获取字符串的函数 (Get language string function)
const char* getLangString(L_StringID stringID);
void setLanguage(Language lang);
Language getLanguage();
const char* getLangDesc(Language lang);


#endif // LANGUAGE_H
