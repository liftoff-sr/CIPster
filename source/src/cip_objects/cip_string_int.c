/*
 * cip_string_i.c
 *
 *  Created on: Apr 21, 2015
 *      Author: Aleksey Timin
 *		(c) BPA, Ltd
 */
#include "cip/cip_string_int.h"
#include "cipster_api.h"
#include "cipcommon.h"
#include "string.h"

S_CIP_String_Int* cip_string_int_create(const char* str, const char* lang, EIP_UINT16 charset)
{
    S_CIP_String_Int *stri = IApp_CipCalloc(1, sizeof(S_CIP_String_Int));
    stri->string_num = 1;
    stri->strings = IApp_CipCalloc(1, sizeof(S_CIP_String_Int_Content));
    strcpy((char *)stri->strings[0].lang, lang);
    stri->strings[0].datatype = CIP_STRING;
    stri->strings[0].charset = charset;

    S_CIP_String *content = IApp_CipCalloc(1, sizeof(S_CIP_String));
    content->Length = strlen(str);
    content->String = IApp_CipCalloc(content->Length, sizeof(EIP_BYTE));
    strcpy((char*)content->String, str);

    stri->strings[0].content = content;

    return stri;
}

S_CIP_String_Int* cip_string_int_create_eng(const char* str)
{
    return cip_string_int_create(str, "eng", 4);
}

S_CIP_String_Int* cip_string_int_create_from_ary(EIP_UINT8 **data, EIP_UINT32 *count)
{
    S_CIP_String_Int *stri = IApp_CipCalloc(1, sizeof(S_CIP_String_Int));
    EIP_INT8 i;
    EIP_UINT32 c;

    c = 0;
    c += decodeData(CIP_USINT, (void *)&stri->string_num, data);
    stri->strings = IApp_CipCalloc(stri->string_num, sizeof(S_CIP_String_Int_Content));

    for (i = 0; i < stri->string_num; ++i) {


        c += decodeData(CIP_USINT, (void *)&stri->strings[i].lang[0], data);
        c += decodeData(CIP_USINT, (void *)&stri->strings[i].lang[1], data);
        c += decodeData(CIP_USINT, (void *)&stri->strings[i].lang[2], data);

        c += decodeData(CIP_USINT, (void *)&stri->strings[i].datatype, data);
        c += decodeData(CIP_UINT, (void *)&stri->strings[i].charset, data);

        if (stri->strings[i].datatype != CIP_STRING) {
            /* Support only the standard string type */

            cip_string_int_free(stri);
            stri = NULL;
        }
        else {
            S_CIP_String *content = IApp_CipCalloc(1, sizeof(S_CIP_String));

            c += decodeData(CIP_UINT, (void *)&content->Length, data);

            content->String = IApp_CipCalloc(content->Length, sizeof(EIP_BYTE));
            memcpy(content->String, *data, content->Length);

            stri->strings[i].content = content;

            c += content->Length;
            *data += content->Length;
        }
    }

    if (count != NULL) *count = c;

    return stri;
}

EIP_UINT8* cip_string_int_cmp(S_CIP_String_Int* s1, S_CIP_String_Int* s2)
{
    // TODO: This code should be considered only as a stub.
    return strcmp((char *)s1->strings[0].content->String, (char *)s2->strings[0].content->String) == 0 ? 0 : 1;
}

void cip_string_int_free(S_CIP_String_Int* stri)
{
    EIP_UINT8 i;

    for (i = 0; i < stri->string_num; ++i) {
        IApp_CipFree(stri->strings[i].content->String);
        IApp_CipFree(stri->strings[i].content);
    }

    IApp_CipFree(stri->strings);
    IApp_CipFree(stri);
}
