/*
 * cip_string_i.h
 *
 *  Created on: Apr 21, 2015
 *      Author: Aleksey Timin
 *		(c) BPA, Ltd
 */

#ifndef INC_CIP_CIP_STRING_I_H_
#define INC_CIP_CIP_STRING_I_H_

#include "ciptypes.h"

typedef struct {
	EIP_BYTE lang[3];
	EIP_BYTE datatype;
	EIP_UINT16 charset;
	S_CIP_String *content;
} S_CIP_String_Int_Content;

/* International string. See spec. C-4.3 */
typedef struct {
	EIP_BYTE string_num;
	S_CIP_String_Int_Content *strings;
} S_CIP_String_Int;

/*! Create an instance of CIP STRINGI
 *
 * Create an instance of CIP STRINGI which contains the string str
 * @param str the contained string
 * @param lang the language in ISO 639-2/T(e.g 'eng')
 * @param charset the code from IANA MIB Printer Codes (RFC 1759)
 *
 * TODO Currently the function support only CIP_STRING type
 */
S_CIP_String_Int* cip_string_int_create(const char* str, const char* lang, EIP_UINT16 charset);

/*! Create an instance of CIP STRINGI
 *
 * Create an instance of CIP STRINGI which contains the string str with default settings for English
 */
S_CIP_String_Int* cip_string_int_create_eng(const char* str);

/*! Create an instance from decoded data
 *
 *  The function uses malloc inside, so an user should free the created object
 *
 */
S_CIP_String_Int* cip_string_int_create_from_ary(EIP_UINT8 **data, EIP_UINT32 *count);

/*! Free an instance of CIP STRINGI
 *
 */

/*!	Compare two international strings
 *
 * 	@return 0 if s1 = s2
 *			-1 s1 != s2
 */
EIP_UINT8* cip_string_int_cmp(S_CIP_String_Int* s1, S_CIP_String_Int* s2);

void cip_string_int_free(S_CIP_String_Int* stri);

#endif /* INC_CIP_CIP_STRING_I_H_ */
