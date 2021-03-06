/* This is a managed file. Do not delete this comment. */

#include <corto.httpclient>
#include <curl/curl.h>
#define INITIAL_BODY_BUFFER_SIZE (512)
#define DEFAULT_CONNECT_TIMEOUT 500
#define DEFAULT_TIMEOUT 300 * 1000
struct url_data {
    size_t  size;
    char*   buffer;
};

/* Local Thread Logging Support */
static ut_tls HTTPCLIENT_KEY_LOGGER;

typedef struct httpclient_Logger_s {
    ut_strbuf    buffer;
    bool            set;
} *httpclient_Logger;

/* Local Configuration */
static ut_tls HTTPCLIENT_KEY_CONFIG;

typedef struct httpclient_Config_s {
    int32_t     timeout;
    int32_t     connect_timeout;
    char* user;
    char* password;
    ut_strbuf header;
    uint32_t header_count;
    ut_strbuf data;
    uint32_t data_count;
    struct curl_slist *headers;
} *httpclient_Config;

size_t write_data(
    void *ptr,
    size_t size,
    size_t nmemb,
    struct url_data *data)
{
    size_t index = data->size;
    size_t n = (size * nmemb);
    char* tmp;
    data->size += (size * nmemb);
    tmp = corto_realloc(data->buffer, data->size + 1);
    if (tmp) {
        data->buffer = tmp;
    } else {
        if (data->buffer) {
            corto_dealloc(data->buffer);
        }

        goto error;
    }

    memcpy((data->buffer + index), ptr, n);
    data->buffer[data->size] = '\0';
    return size * nmemb;
error:
    return 0;
}

static
int16_t httpclient_log(
    CURL *handle,
    curl_infotype type,
    char *data,
    size_t size,
    void *userp);

httpclient_Config httpclient_Config_get(void);

int16_t httpclient_log_config(
    CURL *curl)
{
    if (ut_log_verbosityGet() <= UT_TRACE) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, httpclient_log);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        httpclient_Logger s = (httpclient_Logger)ut_tls_get(
            HTTPCLIENT_KEY_LOGGER);
        if (!s) {
            s = (httpclient_Logger)malloc(sizeof(struct httpclient_Logger_s));
            if (!s) {
                ut_throw("Failed to initialize logger data.");
                goto error;
            }

            s->set = false;
            s->buffer = UT_STRBUF_INIT;
            if (ut_tls_set(HTTPCLIENT_KEY_LOGGER, (void *)s)) {
                ut_throw("Failed to set TLS logger data");
                goto error;
            }

        }

        else {
            ut_strbuf_reset(&s->buffer);
        }

    }

    return 0;
error:
    return -1;
}

void httpclient_log_print(void)
{
    httpclient_Logger s = (httpclient_Logger)ut_tls_get(
        HTTPCLIENT_KEY_LOGGER);
    if (s) {
        if (s->set) {
            ut_trace("LibCurl:\n%s====> LibCurl Complete.",
                ut_strbuf_get(&s->buffer));
            s->set = false;
        }
    }
}

void httpclient_timeout_config(
    CURL *curl)
{
    long to = httpclient_get_timeout();
    if (curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, to) != CURLE_OK) {
        ut_error("Failed to set CURLOPT_TIMEOUT_MS.");
    }

    long cto = httpclient_get_connect_timeout();
    if (curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, cto) != CURLE_OK) {
        ut_error("Failed to set CURLOPT_CONNECTTIMEOUT_MS.");
    }
}

int16_t httpclient_auth_config(
    CURL *curl)
{
    httpclient_Config cfg = httpclient_Config_get();
    if (!cfg) {
        goto noauth;
    }

    if (!cfg->user || !cfg->password) {
        /* Auth Not configured */
        goto noauth;
    }

    if (curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->user) != CURLE_OK) {
        ut_throw("Failed to set CURLOPT_USERNAME.");
        goto error;
    }

    if (curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg->password) != CURLE_OK) {
        ut_throw("Failed to set CURLOPT_PASSWORD.");
        goto error;
    }

noauth:
    return 0;
error:
    return -1;
}

httpclient_Result httpclient_get(
    const char *url,
    const char *fields)
{
    httpclient_Result result = {0, NULL};
    CURL *curl = curl_easy_init();
    if (!curl) {
        ut_throw("could not init curl");
        goto error;
    }

    /* Build URL with Fields concatenated as parameters */
    corto_string urlParams = NULL;
    if ((fields) && (strlen(fields) > 0)) {
        urlParams = ut_asprintf("%s&%s", url, fields);
    }

    struct url_data data = {0, NULL};
    data.buffer = corto_alloc(INITIAL_BODY_BUFFER_SIZE);
    if (!data.buffer) {
        goto error;
    }

    httpclient_log_config(curl);
    data.buffer[0] = '\0';
    if (urlParams) {
        curl_easy_setopt(curl, CURLOPT_URL, urlParams);
    }

    else {
        curl_easy_setopt(curl, CURLOPT_URL, url);
    }

    if (httpclient_auth_config(curl)) {
        goto error;
    }
    httpclient_timeout_config(curl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        ut_warning("curl_easy_perform() failed: %s", curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    result.response = data.buffer;
    curl_easy_cleanup(curl);
    if (urlParams) {
        corto_dealloc(urlParams);
    }

    httpclient_log_print();
    return result;
error:
    return (httpclient_Result){0, NULL};
}

httpclient_Result httpclient_post_impl(
    const char *url,
    CURL *curl)
{
    httpclient_Config cfg = httpclient_Config_get();
    if (!cfg) {
        goto error;
    }

    httpclient_Result result = {0, NULL};
    curl_easy_setopt(curl, CURLOPT_URL, url);

    char *fields = ut_strbuf_get(&cfg->data);
    if (fields) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fields);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(fields));
        printf("fields = %s\n", fields);
    }

    struct curl_slist *headers = (struct curl_slist *)httpclient_get_headers();
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    struct url_data data = {0, NULL};
    data.buffer = corto_alloc(INITIAL_BODY_BUFFER_SIZE);
    if (!data.buffer) {
        goto error;
    }

    if (httpclient_auth_config(curl)) {
        goto error;
    }
    httpclient_timeout_config(curl);
    httpclient_log_config(curl);

    data.buffer[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        ut_warning("curl_easy_perform() failed: %s", curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    result.response = data.buffer;
    curl_easy_cleanup(curl);
    httpclient_log_print();

    free(fields);

    return result;
error:
    return (httpclient_Result){0, NULL};
}

httpclient_Result httpclient_post(
    const char *url)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        ut_throw("Could not init curl");
        goto error;
    }

    return httpclient_post_impl(url, curl);
error:
    return (httpclient_Result){0, NULL};
}

httpclient_Result httpclient_post_body(
    const char *url,
    const char *fields)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        ut_throw("Could not init curl");
        goto error;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fields);

    return httpclient_post_impl(url, curl);
error:
    return (httpclient_Result){0, NULL};
}

httpclient_Result httpclient_post_json(
    const char *url,
    const char *fields)
{
    if (httpclient_append_headers("Accept: application/json")) {
        ut_throw("Failed to set HTTP headers.");
        goto error;
    }

    if (httpclient_append_headers("Accept: application/json")) {
        ut_throw("Failed to set HTTP headers.");
        goto error;
    }

    return httpclient_post_body(url, fields);
error:
    return (httpclient_Result){0, NULL};
}

static void httpclient_tlsConfigFree(
    void *o)
{
    httpclient_Config config = (httpclient_Config)o;
    if (config) {
        if (config->user) {
            corto_dealloc(config->user);
        }

        if (config->password) {
            corto_dealloc(config->password);
        }

        free(config);
    }

}

httpclient_Config httpclient_Config__create(void) {
    httpclient_Config o = (httpclient_Config)malloc(
        sizeof(struct httpclient_Config_s));

    if (!o) {
        ut_throw("Failed to initialize configuration data");
        goto error;
    }

    o->user = NULL;
    o->password = NULL;
    o->header = UT_STRBUF_INIT;
    o->data = UT_STRBUF_INIT;
    o->header_count = 0;
    o->data_count = 0;
    o->timeout = DEFAULT_TIMEOUT;
    o->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
    o->headers = NULL;

    if (ut_tls_set(HTTPCLIENT_KEY_CONFIG, (void *)o)) {
        ut_throw("Failed to set TLS connect timeout data");
        free(o);
        o = NULL;
    }

    return o;
error:
    return NULL;
}

httpclient_Config httpclient_Config_get(void) {
    httpclient_Config config = (httpclient_Config)ut_tls_get(
        HTTPCLIENT_KEY_CONFIG);
    if (!config) {
        config = httpclient_Config__create();
    }

    return config;
}

static void httpclient_tlsLoggerFree(void *o) {
    httpclient_Logger data = (httpclient_Logger)o;
    if (data) {
        free(data);
    }

}

int cortomain(int argc, char *argv[]) {

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (ut_tls_new(&HTTPCLIENT_KEY_CONFIG, httpclient_tlsConfigFree)) {
        ut_throw("Failed to initialize config key");
        goto error;
    }

    if (ut_tls_new(&HTTPCLIENT_KEY_LOGGER, httpclient_tlsLoggerFree)) {
        ut_throw("Failed to initialize logger key");
        goto error;
    }

    return 0;
error:
    return -1;
}

int16_t httpclient_log(
    CURL *handle,
    curl_infotype type,
    char *data,
    size_t size,
    void *userp)
{
    httpclient_Logger s = (httpclient_Logger)ut_tls_get(
        HTTPCLIENT_KEY_LOGGER);
    if (!s) {
        ut_error("HTTPClient Logger config uninitialized.");
        goto error;
    }

    s->set = true;
    (void)handle; /* satisfy compiler warning */
    (void)userp;
    switch (type) {
        case CURLINFO_TEXT:
            // ut_info("libcurl InfoText: %s", data);
            ut_strbuf_appendstr(&s->buffer, "Info: ");
            break;
        default: /* in case a new one is introduced to shock us */
            ut_error("Unhandled LibCurl InfoType: \n%s", data);
            return 0;
        case CURLINFO_HEADER_OUT:
            ut_strbuf_appendstr(&s->buffer, "libcurl => Send header");
            break;
        case CURLINFO_DATA_OUT:
            ut_strbuf_appendstr(&s->buffer, "libcurl => Send data");
            break;
        case CURLINFO_SSL_DATA_OUT:
            ut_strbuf_appendstr(&s->buffer, "libcurl => Send SSL data");
            break;
        case CURLINFO_HEADER_IN:
            ut_strbuf_appendstr(&s->buffer, "libcurl => Recv header");
            break;
        case CURLINFO_DATA_IN:
            ut_strbuf_appendstr(&s->buffer, "libcurl => Recv data");
            break;
        case CURLINFO_SSL_DATA_IN:
            ut_strbuf_appendstr(&s->buffer, "libcurl => Recv SSL data");
            break;
    }

    /* Uncomment to debug byte size resolution.
    corto_string bytes = ut_asprintf(" [%ld bytes]", (long)size);
    ut_strbuf_appendstr(&buffer, bytes);
    corto_dealloc(bytes);
    */
    ut_strbuf_appendstr(&s->buffer, data);
    // ut_trace("%s", ut_strbuf_get(&buffer));
    return 0;
error:
    return -1;
}

corto_string httpclient_encode_fields(
    const char *fields)
{
    corto_string encoded = curl_easy_escape(NULL, fields, 0);
    return encoded;
}

/* Maximum time in milliseconds that you allow the libcurl transfer operation
 * to take. Normally, name lookups can take a considerable time and limiting
 * operations to less than a few minutes risk aborting perfectly normal
 * operations. This option may cause libcurl to use the SIGALRM signal to
 * timeout system calls.
 */
int16_t httpclient_set_timeout(
    int32_t timeout)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    config->timeout = timeout;

    return 0;
error:
    return -1;
}

int32_t httpclient_get_timeout(void)
{
    int32_t timeout = DEFAULT_TIMEOUT;

    httpclient_Config config = httpclient_Config_get();
    if (config) {
        timeout = config->timeout;
    }

    return timeout;
}

/* Maximum time, in milliseconds, that the connection phase is allowed to
 * execute before failing to connect to host */
int16_t httpclient_set_connect_timeout(
    int32_t timeout)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    config->connect_timeout = timeout;

    return 0;
error:
    return -1;
}

int32_t httpclient_get_connect_timeout(void)
{
    int32_t timeout = DEFAULT_CONNECT_TIMEOUT;

    httpclient_Config config = httpclient_Config_get();
    if (config) {
        timeout = config->connect_timeout;
    }

    return timeout;
}

int16_t httpclient_set_password(
    const char *password)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    corto_set_str(&config->password, password);

    return 0;
error:
    return -1;
}

corto_string httpclient_get_password(void)
{
    corto_string password = NULL;

    httpclient_Config config = httpclient_Config_get();
    if (config) {
        password = config->password;
    }

    return password;
}

int16_t httpclient_set_user(
    const char *user)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    corto_set_str(&config->user, user);

    return 0;
error:
    return -1;
}

corto_string httpclient_get_user(void)
{
    corto_string user = NULL;

    httpclient_Config config = httpclient_Config_get();
    if (config) {
        user = config->user;
    }

    return user;
}

int16_t httpclient_append_headers(
    const char *data)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    config->headers = curl_slist_append(
        config->headers,
        data);

    return 0;
error:
    return -1;
}

uintptr_t httpclient_get_headers(void)
{
    struct curl_slist *headers = NULL;
    httpclient_Config config = httpclient_Config_get();
    if (config) {
        headers = config->headers;
    }

    return (corto_word)headers;
}

int16_t httpclient_set_auth(
    const char *user,
    const char *password)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    corto_set_str(&config->user, user);
    corto_set_str(&config->password, password);

    return 0;
error:
    return -1;
}

int16_t httpclient_set_field(
    const char *key,
    const char *value)
{
    httpclient_Config config = httpclient_Config_get();
    if (!config) {
        goto error;
    }

    if (config->data_count) {
        ut_strbuf_append(&config->data, "&%s=%s", key, value);
    } else {
        ut_strbuf_append(&config->data, "%s=%s", key, value);
    }

    config->data_count ++;

    return 0;
error:
    return -1;
}

int16_t httpclient_set_header(
    const char *header,
    const char *value)
{
    /* Insert implementation */
}
