/*
 * requests.c -- librequests: libcurl wrapper implementation
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Mark Mossberg <mark.mossberg@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "requests.h"

/*
 * Initializes requests struct data members
 */
CURL *requests_init(REQ *req, char *url)
{
    req->code = 0;
    req->url = url;
    req->text = calloc(1, 1);
    req->size = 0;

    assert(req->text != NULL && "ERROR: Memory allocation failed");

    return curl_easy_init();
}

/*
 * Calls curl clean up and free allocated memory
 */
void requests_close(CURL *curl, REQ *req)
{
    curl_easy_cleanup(curl);
    free(req->text);
}

/*
 * Callback function for requests, may be called multiple times per request.
 * Allocates memory and assembles response data.
 */
size_t callback(char *content, size_t size, size_t nmemb, REQ *userdata)
{
    size_t real_size = size * nmemb;

    userdata->text = realloc(userdata->text, userdata->size + real_size + 1); // null byte!
    assert(userdata->text != NULL && "ERROR: Memory allocation failed");

    userdata->size += real_size;

    char *responsetext = strndup(content, size * nmemb);
    assert(responsetext != NULL && "ERROR: Memory allocation failed");

    strcat(userdata->text, responsetext);

    free(responsetext);
    return size * nmemb;
}

/*
 * Performs GET request and populates req struct text member with request
 * response, code with response code, and size with size of response.
 */
void requests_get(CURL *curl, REQ *req)
{
    assert(req->url != NULL && "ERROR: No URL provided");

    long code = 0;

    common_opt(curl, req);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, req);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    req->code = code;
}

/*
 * Url encoding function. Takes as input an array of char strings and the
 * size of the array. The array should consist of keys and the corresponding
 * value immediately after in the array. There must be an even number of
 * array elements (one value for every key).
 */
char *url_encode(CURL *curl, char **data, int data_size)
{

    // loop through and get total sum of lengths
    size_t total_size = 0;
    int i = 0;
    for (i = 0; i < data_size; i++) {
        char *tmp = data[i];
        size_t tmp_len = strlen(tmp);
        total_size += tmp_len;
    }

    char encoded[total_size]; // clear junk bytes
    snprintf(encoded, total_size, "%s", "");

    // loop in groups of two, assembling key/val pairs
    for (i = 0; i < data_size; i+=2) {
        char *key = data[i];
        char *val = data[i+1];
        int offset = i == 0 ? 2 : 3; // =, \0 and maybe &
        size_t term_size = strlen(key) + strlen(val) + offset;
        char term[term_size];
        if (i == 0)
            snprintf(term, term_size, "%s=%s", key, val);
        else
            snprintf(term, term_size, "&%s=%s", key, val);
        strncat(encoded, term, strlen(term));
    }

    char *full_encoded = curl_easy_escape(curl, encoded, strlen(encoded));

    return full_encoded;
}

void requests_post(CURL *curl, REQ *req, char **data, int data_size)
{
    requests_pt(curl, req, data, data_size, NULL, 0, 0);
}

void requests_put(CURL *curl, REQ *req, char **data, int data_size)
{
    requests_pt(curl, req, data, data_size, NULL, 0, 1);
}

void requests_post_headers(CURL *curl, REQ *req, char **data, int data_size,
                           char **headers, int headers_size)
{
    requests_pt(curl, req, data, data_size, headers, headers_size, 0);
}

void requests_put_headers(CURL *curl, REQ *req, char **data, int data_size,
                          char **headers, int headers_size)
{
    requests_pt(curl, req, data, data_size, headers, headers_size, 1);
}

/*
 * Utility function that performs POST or PUT request using supplied data and
 * populates req struct text member with request response, code with response
 * code, and size with size of response. To submit no data, use NULL for data,
 * and 0 for data_size.
 *
 * Since PUT and POST are only semantically different, when put_flag is 1,
 * a PUT request will be submitted instead of post. When it's 0, will use a
 * normal POST.
 *
 * Typically this function isn't used directly, use requests_post() or
 * requests_put() instead.
 */
void requests_pt(CURL *curl, REQ *req, char **data, int data_size,
                 char **headers, int headers_size, int put_flag)
{
    char *ua = user_agent();
    char *encoded = NULL;
    struct curl_slist *slist = NULL;
    long code = 0;

    assert((put_flag == 0 || put_flag == 1) &&
           "ERROR: Invalid PUT request flag");

    // body data
    if (data != NULL) {
        assert(data_size % 2 == 0 && "ERROR: Data size must be even");

        encoded = url_encode(curl, data, data_size);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, encoded);
    } else {
        // content length header defaults to -1, which causes request to fail
        // sometimes, so we need to manually set it to 0
        slist = curl_slist_append(slist, "Content-Length: 0");
        if (headers == NULL)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }

    // headers
    if (headers != NULL) {
        int i = 0;
        for (i = 0; i < headers_size; i++) {
            slist = curl_slist_append(slist, headers[i]);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }

    common_opt(curl, req);
    if (put_flag)
        // use custom request instead of dedicated PUT, because dedicated
        // PUT doesn't work with arbitrary request body data
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    else
        curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    req->code = code;

    if (encoded != NULL)
        curl_free(encoded);
    if (slist != NULL)
        curl_slist_free_all(slist);
    free(ua);
}

/*
 * Utility function for executing common curl options.
 */
void common_opt(CURL *curl, REQ *req)
{
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, req);
}

/*
 * Utility function for creating custom user agent.
 */
char *user_agent()
{
    int ua_size = 3; // ' ', \, \0
    char *basic = "librequests/0.1";
    ua_size += strlen(basic);
    struct utsname name;
    uname(&name);
    char *kernel = name.sysname;
    ua_size += strlen(kernel);
    char *version = name.release;
    ua_size += strlen(version);

    char *ua = malloc(ua_size);
    assert(ua != NULL && "ERROR: Memory allocation failed");
    snprintf(ua, ua_size, "%s %s/%s", basic, kernel, version);

    return ua;
}
