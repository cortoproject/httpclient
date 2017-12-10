/* This is a managed file. Do not delete this comment. */

#include <corto/httpclient/httpclient.h>
#include <curl/curl.h>
#define INITIAL_BODY_BUFFER_SIZE (512)
#define DEFAULT_CONNECT_TIMEOUT 500
#define DEFAULT_TIMEOUT 300 * 1000

struct url_data {
    size_t  size;
    char*   buffer;
};

/* Local Thread Logging Support */
static corto_tls HTTPCLIENT_KEY_LOGGER;
typedef struct httpclient_Logger_s {
    corto_buffer    buffer;
    bool            set;
} *httpclient_Logger;

/* Local Configuration */
static corto_tls HTTPCLIENT_KEY_CONFIG;
typedef struct httpclient_Config_s {
    int32_t     timeout;
    int32_t     connectTimeout;
} *httpclient_Config;

size_t write_data(void *ptr, size_t size, size_t nmemb, struct url_data *data) {
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

int16_t httpclient_log_config(
    CURL *curl)
{
    if (corto_log_verbosityGet() <= CORTO_TRACE) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, httpclient_log);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        httpclient_Logger s = (httpclient_Logger)corto_tls_get(
            HTTPCLIENT_KEY_LOGGER);
        if (!s) {
            s = (httpclient_Logger)malloc(sizeof(struct httpclient_Logger_s));
            if (!s) {
                corto_throw("Failed to initialize logger data.");
                goto error;
            }
            s->set = false;
            s->buffer = CORTO_BUFFER_INIT;

            if (corto_tls_set(HTTPCLIENT_KEY_LOGGER, (void *)s)) {
                corto_throw("Failed to set TLS logger data");
                goto error;
            }
        }
        else {
            corto_buffer_reset(&s->buffer);
        }

    }

    return 0;
error:
    return -1;
}

void httpclient_log_print(void)
{
    httpclient_Logger s = (httpclient_Logger)corto_tls_get(
        HTTPCLIENT_KEY_LOGGER);
    if (s) {
        if (s->set) {
            corto_trace("LibCurl:\n%s====> LibCurl Complete.",
                corto_buffer_str(&s->buffer));
            s->set = false;
        }
    }
}

void httpclient_timeout_config(
    CURL *curl)
{
    long to = httpclient_getTimeout();
    if (curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, to) != CURLE_OK) {
        corto_error("Failed to set CURLOPT_TIMEOUT_MS.");
    }

    long cto = httpclient_getConnectTimeout();
    if (curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, cto) != CURLE_OK) {
        corto_error("Failed to set CURLOPT_CONNECTTIMEOUT_MS.");
    }

}

httpclient_Result httpclient_get(
    corto_string url,
    corto_string fields)
{
    httpclient_Result result = {0, NULL};
    CURL *curl = curl_easy_init();
    if (!curl) {
        corto_throw("could not init curl");
        goto error;
    }

    /* Build URL with Fields concatenated as parameters */
    corto_string urlParams = NULL;
    if ((fields) && (strlen(fields) > 0)) {
        urlParams = corto_asprintf("%s&%s", url, fields);
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

    httpclient_timeout_config(curl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        corto_throw("curl_easy_perform() failed: %s", curl_easy_strerror(res));
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

httpclient_Result httpclient_post(
    corto_string url,
    corto_string fields)
{
    httpclient_Result result = {0, NULL};
    CURL* curl = curl_easy_init();
    if (!curl) {
        corto_throw("Could not init curl");
        goto error;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    if (fields) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fields);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(fields));
    }

    struct url_data data = {0, NULL};
    data.buffer = corto_alloc(INITIAL_BODY_BUFFER_SIZE);
    if (!data.buffer) {
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
        corto_throw("curl_easy_perform() failed: %s", curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    result.response = data.buffer;
    curl_easy_cleanup(curl);
    httpclient_log_print();
    return result;
error:
    return (httpclient_Result){0, NULL};
}


static void httpclient_tlsConfigFree(void *o) {
    httpclient_Config config = (httpclient_Config)o;
    if (config) {
        free(config);
    }
}

static void httpclient_tlsLoggerFree(void *o) {
    httpclient_Logger data = (httpclient_Logger)o;
    if (data) {
        free(data);
    }
}

int cortomain(int argc, char *argv[]) {

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (corto_tls_new(&HTTPCLIENT_KEY_CONFIG, httpclient_tlsConfigFree)) {
        corto_throw("Failed to initialize config key");
        goto error;
    }

    if (corto_tls_new(&HTTPCLIENT_KEY_LOGGER, httpclient_tlsLoggerFree)) {
        corto_throw("Failed to initialize logger key");
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
    httpclient_Logger s = (httpclient_Logger)corto_tls_get(
        HTTPCLIENT_KEY_LOGGER);
    if (!s) {
        corto_error("HTTPClient Logger config uninitialized.");
        goto error;
    }

    s->set = true;
    (void)handle; /* satisfy compiler warning */
    (void)userp;

    switch (type) {
        case CURLINFO_TEXT:
            // corto_info("libcurl InfoText: %s", data);
            corto_buffer_appendstr(&s->buffer, "Info: ");
            break;
        default: /* in case a new one is introduced to shock us */
            corto_error("Unhandled LibCurl InfoType: \n%s", data);
            return 0;

        case CURLINFO_HEADER_OUT:
            corto_buffer_appendstr(&s->buffer, "libcurl => Send header");
            break;
        case CURLINFO_DATA_OUT:
            corto_buffer_appendstr(&s->buffer, "libcurl => Send data");
            break;
        case CURLINFO_SSL_DATA_OUT:
            corto_buffer_appendstr(&s->buffer, "libcurl => Send SSL data");
            break;
        case CURLINFO_HEADER_IN:
            corto_buffer_appendstr(&s->buffer, "libcurl => Recv header");
            break;
        case CURLINFO_DATA_IN:
            corto_buffer_appendstr(&s->buffer, "libcurl => Recv data");
            break;
        case CURLINFO_SSL_DATA_IN:
            corto_buffer_appendstr(&s->buffer, "libcurl => Recv SSL data");
            break;
    }

    /* Uncomment to debug byte size resolution.
    corto_string bytes = corto_asprintf(" [%ld bytes]", (long)size);
    corto_buffer_appendstr(&buffer, bytes);
    corto_dealloc(bytes);
    */
    corto_buffer_appendstr(&s->buffer, data);
    // corto_trace("%s", corto_buffer_str(&buffer));
    return 0;
error:
    return -1;
}

corto_string httpclient_encodeFields(
    corto_string fields)
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
int16_t httpclient_setTimeout(
    int32_t timeout)
{
    httpclient_Config config = (httpclient_Config)corto_tls_get(
        HTTPCLIENT_KEY_CONFIG);
    if (!config) {
        config = (httpclient_Config)malloc(sizeof(struct httpclient_Config_s));
        if (!config) {
            corto_throw("Failed to initialize configuration data.");
            goto error;
        }
        config->timeout  = DEFAULT_CONNECT_TIMEOUT;
    }

    config->timeout = timeout;

    if (corto_tls_set(HTTPCLIENT_KEY_CONFIG, (void *)config)) {
        corto_throw("Failed to set TLS connect timeout data");
        goto error;
    }

    return 0;
error:
    return -1;
}

int32_t httpclient_getTimeout(void)
{
    int32_t timeout = DEFAULT_TIMEOUT;

    httpclient_Config config = (httpclient_Config)corto_tls_get(
        HTTPCLIENT_KEY_CONFIG);
    if (config) {
        timeout = config->timeout;
    }

    return timeout;
}

/* Maximum time, in milliseconds, that the connection phase is allowed to
 * execute before failing to connect to host */
int16_t httpclient_setConnectTimeout(
    int32_t timeout)
{
    httpclient_Config config = (httpclient_Config)corto_tls_get(
        HTTPCLIENT_KEY_CONFIG);
    if (!config) {
        config = (httpclient_Config)malloc(sizeof(struct httpclient_Config_s));
        if (!config) {
            corto_throw("Failed to initialize configuration data");
            goto error;
        }
        config->timeout  = DEFAULT_TIMEOUT;
    }

    config->connectTimeout = timeout;

    if (corto_tls_set(HTTPCLIENT_KEY_CONFIG, (void *)config)) {
        corto_throw("Failed to set TLS connect timeout data");
        goto error;
    }

    return 0;
error:
    return -1;
}

int32_t httpclient_getConnectTimeout(void)
{
    int32_t timeout = DEFAULT_CONNECT_TIMEOUT;

    httpclient_Config config = (httpclient_Config)corto_tls_get(
        HTTPCLIENT_KEY_CONFIG);
    if (config) {
        timeout = config->connectTimeout;
    }

    return timeout;
}
