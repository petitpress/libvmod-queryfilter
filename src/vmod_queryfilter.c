/*=============================================================================
 * libvmod-queryfilter: Simple VMOD for filtering/sorting query strings
 *
 * Copyright © 2014-2020 The New York Times Company
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *===========================================================================*/

#include "config.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

/*--- Varnish 3.x: ---*/
#if VARNISH_API_MAJOR == 3
#include "vrt.h"
#include "vcc_if.h"
#include "cache.h"
typedef struct sess req_ctx;
#endif /* VARNISH_API_MAJOR == 3 */

/*--- Varnish 4.x and 5.x ---*/
#if (VARNISH_API_MAJOR == 4 || VARNISH_API_MAJOR == 5 )
#include "vrt.h"
#include "vcc_if.h"
#include "vre.h"
#include "cache/cache.h"
typedef const struct vrt_ctx req_ctx;
#endif /* (VARNISH_API_MAJOR == 4 || VARNISH_API_MAJOR == 5 ) */

/*--- Varnish 6.x, 7.x and 8.x ---*/
#if (VARNISH_API_MAJOR == 6 || VARNISH_API_MAJOR == 7 || VARNISH_API_MAJOR == 8)
#include "cache/cache.h"
#include "vcl.h"
#include "vre.h"
#include "vas.h"
#include "vsb.h"
#include "vcc_if.h"
typedef const struct vrt_ctx req_ctx;
#endif /* (VARNISH_API_MAJOR == 6 || VARNISH_API_MAJOR == 7 || VARNISH_API_MAJOR == 8) */

/* WS_Reserve was deprecated in Varnish 6.3.0: */
#if (VARNISH_API_MAJOR < 6) || (VARNISH_API_MAJOR == 6 && VARNISH_API_MINOR < 3)
#define WS_ReserveAll(ws) \
    WS_Reserve(ws, 0)
#endif /* Varnish Version >= 6.3 */

/* WS_Reservation() was introduced in Varnish 6.4 to replace direct ws->f
 * access (WS_Front() was removed in 7.0). For Varnish < 6.4, define a shim
 * via the still-public ws->f member. */
#if (VARNISH_API_MAJOR < 6) || (VARNISH_API_MAJOR == 6 && VARNISH_API_MINOR < 4)
#define WS_Reservation(ws) ((ws)->f)
#endif


/** Alignment macros ala varnish internals: */
#define PALIGN_SIZE     (sizeof(void*))
#define PALIGN_DELTA(p) (PALIGN_SIZE - (((uintptr_t)p) % PALIGN_SIZE))

/** Simple struct used for one-time query parameter tokenization. */
typedef struct query_param {
    char* name;
    char* value;
} query_param_t;

/** URL decode a string in-place.
 * @param str the string to decode
 * @return the length of the decoded string
 */
static int
url_decode_inplace(char* str)
{
    char* src = str;
    char* dst = str;

    if (!str) return 0;

    while (*src) {
        if (*src == '%' && src[1] && src[2] &&
            isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int high = isdigit((unsigned char)src[1]) ? (src[1] - '0') : (toupper((unsigned char)src[1]) - 'A' + 10);
            int low  = isdigit((unsigned char)src[2]) ? (src[2] - '0') : (toupper((unsigned char)src[2]) - 'A' + 10);
            *dst++ = (char)((high << 4) | low);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return dst - str;
}

/** Maximum byte length of a parameter name that will be decoded for comparison.
 * Names longer than this are compared as-is (without URL-decoding). */
#define MAX_DECODED_PARAM_NAME_LEN 255

/** Extract the base parameter name from array notation.
 * Handles both "param[]" and "param[0]", "param[1]" etc.
 * @param param_name the parameter name to process
 * @param base_len pointer to store the base name length
 * @return pointer to start of base name, or NULL if not an array
 */
static const char*
extract_array_base_name_fast(const char* param_name, size_t* base_len)
{
    const char* bracket = strchr(param_name, '[');
    if (!bracket) {
        return NULL;  /* Not an array parameter */
    }

    /* Require exactly one '[...]' group at the end of the name.
     * strchr finds the first ']', so *(end+1) != '\0' rejects anything
     * after the bracket (e.g. "param[0][1]" or "param[]extra"). */
    const char* end = strchr(bracket, ']');
    if (!end || *(end + 1) != '\0') {
        return NULL;  /* Not a valid single-level array notation */
    }

    *base_len = bracket - param_name;
    return param_name;
}

/** Check if two parameter names should be considered the same for array purposes.
 * Temporarily decode parameter names for comparison without modifying originals.
 * @param filter_name the name from the filter list
 * @param param_name the name from the query string (may be URL-encoded)
 * @param arrays_enabled whether array processing is enabled
 * @return 1 if they match, 0 otherwise
 */
static int
param_names_match_with_decoding(const char* filter_name, const char* param_name, unsigned arrays_enabled)
{
    /* Each decoded byte is at most 3 encoded chars (%XX), so size the buffer
     * for the worst-case encoded input.  This handles names where the encoded
     * form exceeds MAX_DECODED_PARAM_NAME_LEN but the decoded form does not. */
    char decoded_name[MAX_DECODED_PARAM_NAME_LEN * 3 + 1];
    const char* name_to_compare = param_name;

    size_t len = strlen(param_name);
    if (len <= MAX_DECODED_PARAM_NAME_LEN * 3) {
        memcpy(decoded_name, param_name, len + 1);
        int decoded_len = url_decode_inplace(decoded_name);
        /* Only use the decoded form if it fits within the comparison limit;
         * pathologically long names fall back to raw comparison. */
        if ((size_t)decoded_len <= MAX_DECODED_PARAM_NAME_LEN) {
            name_to_compare = decoded_name;
        }
    }

    if (!arrays_enabled) {
        return strcmp(filter_name, name_to_compare) == 0;
    }

    size_t filter_base_len, param_base_len;
    const char* filter_base = extract_array_base_name_fast(filter_name, &filter_base_len);
    const char* param_base = extract_array_base_name_fast(name_to_compare, &param_base_len);

    /* If filter expects arrays, match base names */
    if (filter_base && param_base) {
        return filter_base_len == param_base_len &&
               strncmp(filter_base, param_base, filter_base_len) == 0;
    }

    /* If filter expects arrays but param is not array, no match */
    if (filter_base && !param_base) {
        return 0;
    }

    /* If filter doesn't expect arrays but param is an array, no match.
     * A plain filter "foo" must not implicitly absorb "foo[]" or "foo[0]";
     * callers who want array params must include "foo[]" in the filter list. */
    if (!filter_base && param_base) {
        return 0;
    }

    /* Both are regular parameters */
    return strcmp(filter_name, name_to_compare) == 0;
}

/** Query string tokenizer. This function takes a query string as input, and
 * yields array of name/value pairs. Allocation happens inside the
 * reserved workspace, pointed to by *ws_free. On error, no space is consumed.
 *
 * @param result pointer to query_param_t* at the head of the array
 * @param ws_free pointer to char* at the head of the reserved workspace
 * @param ws_remain the amount of reserved workspace remaining, in bytes
 * @param query_str the query string to tokenize
 * @return the number of non-empty query params or -1 on OOM
 */
static int
tokenize_querystring(query_param_t** result, char** ws_free, unsigned* remain, char* query_str)
{
    int no_param = 0;
    char* save_ptr;
    char* param_str;

    /* Temporary copies of workspace head + allocation counter: */
    char* ws_free_temp = *ws_free;
    unsigned remain_temp = *remain;
    query_param_t* head = NULL;

    /* Move the free pointer up so that query_param_t objects allocated on
     * WS storage are properly aligned: */
    unsigned align_adjust = PALIGN_DELTA(ws_free_temp);
    ws_free_temp += align_adjust;
    remain_temp -= align_adjust;

    /* Tokenize the query parameters into an array: */
    for(param_str = strtok_r(query_str, "&", &save_ptr); param_str;
        param_str = strtok_r(NULL, "&", &save_ptr))
    {
        /* If we run out of space at any point, just bail.
         * Note that in this case, we don't update ws_free or remain so that
         * the space we've consumed thus far is returned to the workspace. */
        if( remain_temp < sizeof(query_param_t) ) {
            (*result) = NULL;
            return -1;
        };

        /* "Allocate" space at the head of the workspace and place a node: */
        query_param_t* param = (query_param_t*)ws_free_temp;
        param->name = param_str;
        /* TODO: will varnish filter malformed queries, e.g.: "?=&"?
         * Else: this needs some more rigor:
         */
        param->value = strchr(param_str,'=');
        if( param->value ) {
            *(param->value++) = '\0';

            /* If the actual value is the end of the string
             * or the beginning of another parameter, set the
             * parameter to NULL.
             */
            if( *(param->value) == '\0' || *(param->value) == '&') {
                param->value = NULL;
            };
        };

        if( !head ) {
            head = param;
        };
        remain_temp -= sizeof(query_param_t);
        ws_free_temp += sizeof(query_param_t);
        no_param++;
    };

    (*result) = head;
    (*ws_free) = ws_free_temp;
    (*remain) = remain_temp;
    return no_param;
}

/* Hacky workspace string copy. We pray for inline. ;)
 *
 * @param ws_free pointer to char* at the head of the reserved workspace
 * @param ws_remain the amount of reserved workspace remaining, in bytes
 * @return on success the pointer to the new string
 */
inline static char*
strtmp_append(char** ws_free, unsigned* remain, const char* str_in)
{
    char* dst = NULL;
    size_t buf_size = strlen(str_in) + 1;
    if( buf_size <= *remain ) {
        memcpy(*ws_free,str_in,buf_size);
        dst = *ws_free;
        *ws_free += buf_size;
        *remain -= buf_size;
    };
    return dst;
}

/** Entrypoint for filterparams.
 *
 * Notes:
 * 1. We copy the URI as working space for the output URI, assuming that the
 *    filtered URI will always be less than or equal to the input URI size.
 * 2. Tokenize the querystring *once* and store it as an array
 * 3. Tokenize and iterate over the input parameters, copying matching query
 *    parameters to the end of our allocated space; the terms end up sorted
 *    as a byproduct.
 * 4. On success, release all but the space occupied by new_uri; on failure
 *    release all workspace memory that was allocated.
 *
 * @param sp Varnish Session Pointer
 * @param uri The input URI
 * @param params_in a comma separated list of query parameters to *include*
 * @return filtered URI on success; NULL on failure
 */
const char*
vmod_filterparams(req_ctx* sp, const char* uri, const char* params_in, unsigned arrays_enabled)
{
    char* saveptr;
    char* new_uri;
    char* new_uri_end;
    char* query_str;
    char* params;
    char* ws_free;
    struct ws* workspace = sp->ws;
    query_param_t* head;
    query_param_t* current;
    const char* filter_name;
    int i;
    int no_param;
    unsigned ws_remain;
    char sep = '?';

    /* Right off, do nothing if there's no query string: */
    query_str = strchr(uri, '?');
    if( query_str == NULL ) {
        return uri;
    }

    /* Reserve the *rest* of the workspace - it's okay, we're gonna release
     * almost all of it in the end ;) */
    ws_remain = WS_ReserveAll(workspace);
    ws_free = WS_Reservation(workspace);

    /* Duplicate the URI, bailing on OOM: */
    new_uri = strtmp_append(&ws_free, &ws_remain, uri);
    if( new_uri == NULL ) {
        goto release_bail;
    };

    /* Terminate the URI at the beginning of the query string: */
    new_uri_end = new_uri + (query_str - uri);
    *new_uri_end = '\0';
    query_str = new_uri_end+1;

    /* If there are no query params, return the sanitized URI: */
    if( *query_str == '\0' ) {
        goto release_okay;
    };

    /* Copy the query string to the head of the workspace: */
    query_str = strtmp_append(&ws_free, &ws_remain, query_str);
    if( !query_str ) {
        goto release_bail;
    };

    /* Copy the params to the head of the workspace: */
    params = strtmp_append(&ws_free, &ws_remain, params_in);
    if( !params) {
        goto release_bail;
    };

    /* Now, tokenize the query string and copy only matching params: */
    no_param = tokenize_querystring(&head, &ws_free, &ws_remain, query_str);
    /* If we ran out of memory. Bail out. */
    if( no_param < 0 ) {
        goto release_bail;
    };

    /* If we only had empty tokens (e.g. "?a=&b=") we're done! */
    if( no_param == 0 ) {
        goto release_okay;
    };

    /* Iterate over the list of parameters, looking for matches and appending
     * them. */
    for(filter_name = strtok_r(params, ",", &saveptr); filter_name;
        filter_name = strtok_r(NULL, ",", &saveptr))
    {
        for(i=0, current=head; i<no_param; ++i, ++current)
        {
            if(!param_names_match_with_decoding(filter_name, current->name, arrays_enabled)) {
                continue;
            };

            if(current->value && (*current->value) != '\0') {
                new_uri_end += sprintf(new_uri_end, "%c%s=%s",
                    sep, current->name, current->value);
            } else {
                /* Empty params have been excluded, so this
                 * is a flag-style query param: */
                new_uri_end += sprintf(new_uri_end, "%c%s",
                    sep, current->name);
            };

            /* After the first param, swap the separator: */
            sep = '&';

            /* Scalar filters break after the first match so that duplicate
             * params (e.g. ?date=old&date=new) are deduplicated to a stable
             * cache key.  Array filters (e.g. urls[]) must keep iterating to
             * collect all elements. */
            if( !arrays_enabled || strchr(filter_name, '[') == NULL ) {
                break;
            }
        };
    };

release_okay:
    /* Include the null terminator in the release (+1).  sprintf writes '\0'
     * at new_uri_end but doesn't count it, so without +1 ws->f lands on that
     * byte and the next workspace allocation (e.g. a VCL syslog string concat)
     * overwrites it. hash_data() then walks past the intended end of req.url
     * and produces an inconsistent hash, causing spurious cache misses. */
    WS_Release(workspace, (new_uri_end-new_uri) + 1);
    return new_uri;

release_bail:
    WS_Release(workspace, 0);
    return NULL;
}

/* EOF */

