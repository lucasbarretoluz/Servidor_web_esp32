/* 
    HTTP web Server
*/

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"

/* Max length a file path can have on storage */
/* Comprimento maximo de um arquivo no armazenamento */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

//-------->apagar<-------------------------------
/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE   (200*1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"
//----------------------------------------------------

/* Scratch buffer size - Tamanho do buffer de trabalho */
#define SCRATCH_BUFSIZE 8192

char parse[100];

/* Usado para o armazenamento dos arquivos */
struct file_server_data
{
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "web_server";
//-----------------------------------------------------------------------------------------
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect"); //status da resposta http
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0); // Response body can be empty
    return ESP_OK;
}
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

static esp_err_t style_css_get_handler(httpd_req_t *req)
{
    extern const unsigned char style_css_start [] asm("_binary_style_css_start");
    extern const unsigned char style_css_end [] asm("_binary_style_css_end");
    const size_t style_css_size = (style_css_end - style_css_start);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css_start, style_css_size);
    return ESP_OK;
}

static esp_err_t logo_agst_get_handler(httpd_req_t *req)
{
    extern const unsigned char logoAGST_png_start [] asm("_binary_logoAGST_png_start");
    extern const unsigned char logoAGST_png_end [] asm("_binary_logoAGST_png_end");
    const size_t logoAGST_png_size = (logoAGST_png_end - logoAGST_png_start);
    httpd_resp_set_type(req, "image/png");
    httpd_resp_send(req, (const char *)logoAGST_png_start, logoAGST_png_size);
    return ESP_OK;
}

static esp_err_t logo_conflex_get_handler(httpd_req_t *req)
{
    extern const unsigned char logoConflex_png_start [] asm("_binary_logoConflex_png_start");
    extern const unsigned char logoConflex_png_end [] asm("_binary_logoConflex_png_end");
    const size_t logoConflex_png_size = (logoConflex_png_end - logoConflex_png_start);
    httpd_resp_set_type(req, "image/png");
    httpd_resp_send(req, (const char *)logoConflex_png_start, logoConflex_png_size);
    return ESP_OK;
}
//-------------------------------------------------------------------------------------------------
// static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
// {
   
    
// }

static esp_err_t login_resp_dir_html(httpd_req_t *req, const char *dirpath)
{

    extern const unsigned char login_html_start [] asm("_binary_login_html_start");
    extern const unsigned char login_html_end [] asm("_binary_login_html_end");
    const size_t login_html_size = (login_html_end - login_html_start);
    
    httpd_resp_send(req, (const char *)login_html_start, login_html_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
//--------------------------------------------------------------------------------------------------
#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension   |   Define o tipo de resposta de acordo com o arquivo */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")){
        return httpd_resp_set_type(req, "application/pdf");
    }
    else if (IS_FILE_EXT(filename, ".html")){
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".jpeg")){
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".ico")){
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if (IS_FILE_EXT(filename, ".js")){
        return httpd_resp_set_type(req, "application/javascript");
    }
    else if (IS_FILE_EXT(filename, ".css")){
        return httpd_resp_set_type(req, "text/css");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}


/* Copies the full path into destination buffer and returns pointer to path (skipping the preceding base path) */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);  //strlen ve o tamanho do arquivo base_path e retorna um inteiro (tam caracteres)
    size_t pathlen = strlen(uri);          

    const char *quest = strchr(uri, '?');       //procura o caracter '?' e retorna o ponteiro para esse local
    if (quest)
    {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash)
    {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize)
    {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);    //copia o conteudo da segunda variavel para  a primeira
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}
//------------------------------------------------------------------------------------------------------------------
static esp_err_t webserver_get_handler (httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;  

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));  

    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }
    
    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/')
    {
        return login_resp_dir_html(req, filepath);
    }
    
    if (stat(filepath, &file_stat) == -1) 
    {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0) 
        {
            return index_html_get_handler(req);
        } 
        else if (strcmp(filename, "/favicon.ico") == 0) 
        {
            return favicon_get_handler(req);
        }
        else if (strcmp(filename, "/logoAGST.png") == 0)
        {
            ESP_LOGI(TAG, "logoAGST!");
            return logo_agst_get_handler(req);
        }
        else if (strcmp(filename, "/logoConflex.png") == 0)
        {
            ESP_LOGI(TAG, "logoConflex!");
            return logo_conflex_get_handler(req);
        }
        else if (strcmp(filename, "/style.css") == 0)
        {
            ESP_LOGI(TAG, "css!");
            return style_css_get_handler(req);
        }
         
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }   
    
    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do
    {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
        {

            fclose(fd);
            ESP_LOGE(TAG, "File sending failed!");
            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

//-----------------------------------------------------------------------------------------------------------
/* Function to start the file server */
esp_err_t start_file_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    /* Validate file storage base path */
    if (!base_path || strcmp(base_path, "/spiffs") != 0)
    {
        ESP_LOGE(TAG, "File server presently supports only '/spiffs' as base path");
        return ESP_ERR_INVALID_ARG;
    }

    if (server_data)
    {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }
    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* URI handler for getting uploaded files */
    httpd_uri_t file_server = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = webserver_get_handler,
        .user_ctx = server_data // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_server);


    return ESP_OK;
}
