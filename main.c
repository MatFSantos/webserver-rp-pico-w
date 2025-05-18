#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

#include "lib/ssd1306.h"
#include "lib/led.h"


// WIFI credentials
#include "credenciais.h"

/** pin definitions */
#define HUMIDITY_SENSOR 27            /* X axis */
#define LM35 26                       /* Y axis */
#define LED_PIN CYW43_WL_GPIO_LED_PIN /* LED of the rp pico w board */

/** global variables */
static volatile double temperature = 0.0;
static volatile double humidity = 0.0;
const double critical_level_UP = 50.0; // critical level of the temperature
const double critical_level_DOWN = 50.0;
ssd1306_t ssd;


/**
 * @brief Function callback to close tcp conection
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb for which data has been acknowledged
 * @param len The amount of bytes acknowledged
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len);

/**
 * @brief Function callback called to accept tcp conection
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param newpcb The new connection pcb
 * @param err An error code if there has been an error accepting.
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

/**
 * @brief Function to make the response string according GET route called
 *
 * @param request The received request data
 * @param buffer point to buffer string that will be used to send data
 * @param buffer_size buffer size
 */
void user_request(char **request, char *buffer, size_t buffer_size);

/**
 * @brief Function callback used to process the received request data
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb which received data
 * @param p The received data (or NULL when the connection has been closed!)
 * @param err An error code if there has been an error receiving
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

/**
 * @brief Initialize the SSD1306 display
 *
 */
void init_display(void);

/**
 * @brief Initialize the all GPIOs that will be used in project
 *      - I2C;
 *      - Blue LED;
 */
void init_gpio(void);

/**
 * @brief Update the display informations
 *
 * @param message the message that will be ploted in display OLED
 * @param y position on vertical that the message will be ploted
 * @param clear if the display must be cleaned
 *
 */
void update_display(char * message, uint8_t y, bool clear);

/**
 * @brief Function to read the humidity sensor
 *
 * @param channel analog channel to read
 *
 */
void read_humidity(uint8_t channel);

/**
 * @brief Function to read the temperature sensor
 *
 * @param channel analog channel to read
 *
 */
void read_temperature(uint8_t channel);

int main() {
    // set clock freq to 128MHz
    bool ok = set_sys_clock_khz(128000, false);
    if (ok)
        printf("clock set to %ld\n", clock_get_hz(clk_sys));
    //Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();
    init_gpio();

    // init adc channels
    adc_init();
    adc_gpio_init(HUMIDITY_SENSOR);
    adc_gpio_init(LM35);

    // get ws and ssd struct
    init_display();
    update_display("WEBSERVER", 18, true);
    update_display("Iniciando...", 28, false);

    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        update_display("Falha", 18, true);
        update_display("iniciar WiFi", 28, false);
        update_display("Aguarde...", 38, false);
        sleep_ms(1000);
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        update_display("WiFi", 18, true);
        update_display("Conectando", 28, false);
        update_display("Aguarde...", 38, false);
        sleep_ms(200);
    }
    update_display("Conectado WiFi", 8, true);

    if (netif_default)
        update_display(ipaddr_ntoa(&netif_default->ip_addr), 18, false);

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        update_display("Falha", 18, true);
        update_display("Criar server", 28, false);
        update_display("Reinicie o RP", 38, false);
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        update_display("Falha", 18, true);
        update_display("Abrir server", 28, false);
        update_display("Reinicie o RP", 38, false);
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    update_display("PORT 80", 28, false);

    // Inicializa o conversor ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true) {
        read_temperature(0);
        read_humidity(1);
        if (temperature >= critical_level_UP && !gpio_get(PIN_BLUE_LED)) // verifica nível crítico da temperatura
            gpio_put(PIN_BLUE_LED, 1); 
        if (temperature <= critical_level_DOWN && gpio_get(PIN_BLUE_LED))
            gpio_put(PIN_BLUE_LED, 0);
        cyw43_arch_poll();  // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);      // Reduz o uso da CPU
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Functions --------------------------------- //

static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    tcp_close(tpcb); // fecha a conexão após o envio da resposta
    return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);   // callback to received
    tcp_sent(newpcb, tcp_sent_callback); // callback to sent
    update_display("   Data sent  ", 48, false);
    return ERR_OK;
}

void user_request(char **request, char *buffer, size_t buffer_size) {
    if (strstr(*request, "GET /device") != NULL) { // rota para trocar o estado do dispositivo
        gpio_put(PIN_BLUE_LED, !gpio_get(PIN_BLUE_LED));
        char json[128];
        snprintf(json, sizeof(json),"{\"status\":%s}",gpio_get(PIN_BLUE_LED) ? "true" : "false");
        snprintf(buffer, buffer_size,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(json), json
        );
    } else if (strstr(*request, "GET /data") != NULL) { // rota para capturar os dados que são mostrados na tela
        char json[128];
        snprintf(json, sizeof(json), "{\"temp\":%.2f,\"humd\":%.2f,\"device\":%s}", temperature, humidity, gpio_get(PIN_BLUE_LED) ? "true" : "false");
        snprintf(buffer, buffer_size,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(json), json
        );
    } else if (strstr(*request, "GET /") != NULL) { // rota home, para retorno da página html
        snprintf(buffer, buffer_size,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>"
            "<html>"
            "<head>"
            "<title>Embarcatech - Monitoramento</title>"
            "<style>"
            "body{background-color:#3b3b39;font-family:Arial,sans-serif;"
            "text-align:center;margin-top:50px;color:white;display:flex;justify-content:center;}"
            "h1{font-size:64px;margin-bottom:30px;}"
            "button{background-color:LightGray;font-size:36px;margin:10px;"
            "padding:20px 40px;border-radius:10px;cursor:pointer;}"
            "p{font-size:12px}"
            "#c{background-color:#1f1f1e;border:1px solid white;padding:10px;}"
            "</style>"
            "</head>"
            "<body>"
            "<div id=\"c\">"
            "<h1>Monitoramento ambiente</h1>"
            "<h3>Temperatura: <span id=\"t\"></span> C</h3>"
            "<h3>Umidade: <span id=\"h\"></span>%%</h3>"
            "<h3>Status do dispositivo: <span id=\"ds\"></span></h3>"
            "<button id=\"d\" onclick=\"sd()\"></button>"
            "<p>Obs: dados atualizados a cada 5 segundos</p>"
            "</div>"
            "<script>"
            "const d=document.getElementById('d');"
            "const ds=document.getElementById('ds');"
            "let v=(dt)=>{"
            "if(dt){"
            "d.textContent = 'Desligar';"
            "ds.textContent = 'ligado';"
            "}else{"
            "d.textContent = 'Ligar';"
            "ds.textContent = 'desligado';"
            "}};"
            "let g=()=>{"
            "fetch('./data').then(r=>r.json()).then(j=>{"
            "document.getElementById('t').textContent=j.temp;"
            "document.getElementById('h').textContent=j.humd;"
            "v(j.device);"
            "});};"
            "setInterval(g,1000);"
            "let sd=()=>{"
            "fetch('./device').then(r=>r.json()).then(j=>{"
            "alert(j.status?'dispositivo ativado':'dispositivo desativado');"
            "v(j.status);"
            "})};"
            "g();"
            "</script>"
            "</body>"
            "</html>"
        );
    } else { // caso não contre a rota chamada, retorna 404
        const char *json = "{\"message\":\"Unknown route\"}";
        snprintf(buffer, buffer_size, // Formatar uma string e armazená-la em um buffer de caracteres
            "HTTP/1.1 404 NOT FOUND\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(json), json
        );
    }
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // monta resposta
    char res[4096];
    user_request(&request, res, 4096);

    tcp_write(tpcb, res, strlen(res), TCP_WRITE_FLAG_COPY); // escreve para envio
    tcp_output(tpcb); //envia dados

    free(request); // libera ponteiro alocado
    pbuf_free(p); // libera um buffer de pacote (pbuf) alocado

    return ERR_OK;
}

void init_display() {
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, I2C_ADDRESS, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
}

void init_gpio() {
    /** initialize i2c communication */
    int baudrate = 400 * 1000; // 400kHz baud rate for i2c communication
    i2c_init(I2C_PORT, baudrate);

    // set GPIO pin function to I2C
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SCL);

    /** initialize BLUE LED */
    gpio_init(PIN_BLUE_LED);
    gpio_set_dir(PIN_BLUE_LED, GPIO_OUT);
}

void update_display(char * message, uint8_t y, bool clear) {
    if(clear)
        ssd1306_fill(&ssd, false);
    ssd1306_rect(&ssd, 0, 0, 128, 64, true, false);
    ssd1306_draw_string(&ssd, message, 64 - (strlen(message) * 4), y);
    // monta resposta
    char res[4096];
    user_request(&request, res, 4096);

void read_humidity(uint8_t channel) {
    adc_select_input(channel);
    double value = (double)adc_read();
    humidity = 100 - (value * (100.0f / 4095.0f)); /* 100% indica nenhuma umidade, 0% indica umidade total*/
}

void read_temperature(uint8_t channel) {
    adc_select_input(channel);
    double value = (double)adc_read();
    /*é adicionado um shift positivo de 1.4V, para que os valores em simulação sejam reais usando o potenciometro dos joysticks */
    value = value - (1400 * 4095 / 3300);
    if (value < 0)
        value = 0;
    temperature = (value * (3300.0f / 4095.0f)) / 10; /* converte a leitura para a temperatura em °C*/
}