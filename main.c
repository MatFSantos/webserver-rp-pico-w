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
ssd1306_t ssd;

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Tratamento do request do usuário
void user_request(char **request);

// Função para inicializar o display OLED
void init_display(void);

// Função que inicia GPIOs usadas no projeto
void init_gpio(void);

// Função que atualiza informações no display OLED
void update_display(char * message, uint8_t y, bool clear);

// Função que lê o pino analógico da humidade
void read_humidity(uint8_t channel);

//Função que lê o pino analógico da temperatura
void read_temperature(uint8_t channel);

// Função principal
int main()
{
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

    while (true)
    {
        /* 
        * Efetuar o processamento exigido pelo cyw43_driver ou pela stack TCP/IP.
        * Este método deve ser chamado periodicamente a partir do ciclo principal 
        * quando se utiliza um estilo de sondagem pico_cyw43_arch 
        */
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);      // Reduz o uso da CPU
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    tcp_close(tpcb); // fecha a conexão após o envio da resposta
    return ERR_OK;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    tcp_sent(newpcb, tcp_sent_callback); // callback para envio
    update_display("Dados enviados", 48, false);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request){

    if (strstr(*request, "GET /on_air") != NULL)
    {
        pwm_set_gpio_level(PIN_BLUE_LED, PWM_WRAP / 4);
        pwm_set_gpio_level(PIN_RED_LED, PWM_WRAP / 4);
        pwm_set_gpio_level(PIN_GREEN_LED, PWM_WRAP / 4);
    } else if (strstr(*request, "GET /off_air") != NULL)
    {
        pwm_set_gpio_level(PIN_BLUE_LED, 0);
        pwm_set_gpio_level(PIN_RED_LED, 0);
        pwm_set_gpio_level(PIN_GREEN_LED, 0);
    }
};


// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);
    read_temperature(0);
    read_humidity(1);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);

    // Cria a resposta HTML
    char html[2048];

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title> Embarcatech - Monitoramento </title>\n"
             "<style>\n"
             "body { background-color:rgb(159, 234, 212); font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
             "h1 { font-size: 64px; margin-bottom: 30px; }\n"
             "button { background-color: LightGray; font-size: 36px; margin: 10px; padding: 20px 40px; border-radius: 10px; }\n"
             "</style>\n"
             "</head>\n"
             "<body>\n"
             "<h1>Monitoramento ambiente</h1>\n"
             "<h3>Temperatura: <span>%.2f</span> graus celcius</h3>\n"
             "<h3>Umidade: <span>%.2f</span>%%</h3>\n"
             "<form action=\"./on_air\"><button>Ligar Ar</button></form>\n"
             "<form action=\"./off_air\"><button>Deligar Ar</button></form>\n"
             "<p>Obs: dados de temperatura e umidade sao atualizados a cada 5 segundos</p>"
             "</body>\n"
             "<script>setInterval(() => {location.href = '/'}, 5000)\n</script>"
             "</html>\n",
             temperature,
             humidity);

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    //libera memória alocada dinamicamente
    free(request);
    
    //libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}

/**
 * @brief Initialize the SSD1306 display
 *
 */
void init_display()
{
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, I2C_ADDRESS, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
}

/**
 * @brief Initialize the all GPIOs that will be used in project
 *      - I2C;
 */
void init_gpio()
{
    /** initialize i2c communication */
    int baudrate = 400 * 1000; // 400kHz baud rate for i2c communication
    i2c_init(I2C_PORT, baudrate);

    // set GPIO pin function to I2C
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SCL);

    /** initialize RGB LED */
    pwm_init_(PIN_BLUE_LED);
    pwm_setup(PIN_BLUE_LED, PWM_DIVISER, PWM_WRAP);
    pwm_start(PIN_BLUE_LED, 0);

    pwm_init_(PIN_RED_LED);
    pwm_setup(PIN_RED_LED, PWM_DIVISER, PWM_WRAP);
    pwm_start(PIN_RED_LED, 0);

    pwm_init_(PIN_GREEN_LED);
    pwm_setup(PIN_GREEN_LED, PWM_DIVISER, PWM_WRAP);
    pwm_start(PIN_GREEN_LED, 0);

    /** initialize button */
    init_push_button(PIN_BUTTON_A);

    /** initialize buzzer */
    pwm_init_(BUZZER);
    pwm_setup(BUZZER, PWM_DIVISER, PWM_WRAP);
    pwm_start(BUZZER, 0);
}

/**
 * @brief Update the display informations
 */
void update_display(char * message, uint8_t y, bool clear)
{
    if(clear)
        ssd1306_fill(&ssd, false);
    ssd1306_rect(&ssd, 0, 0, 128, 64, true, false);
    ssd1306_draw_string(&ssd, message, 64 - (strlen(message) * 4), y);
    ssd1306_send_data(&ssd); // update display
}

/**
 * @brief Function to read the humidity sensor
 *
 * @param channel analog channel to read
 *
 */
void read_humidity(uint8_t channel)
{
    adc_select_input(channel);
    double value = (double)adc_read();
    humidity = 100 - (value * (100.0f / 4095.0f)); /* 100% indica nenhuma umidade, 0% indica umidade total*/
}

/**
 * @brief Function to read the temperature sensor
 *
 * @param channel analog channel to read
 *
 */
void read_temperature(uint8_t channel)
{
    adc_select_input(channel);
    double value = (double)adc_read();
    /*é adicionado um shift positivo de 1.4V, para que os valores em simulação sejam reais usando o potenciometro dos joysticks */
    value = value - (1400 * 4095 / 3300);
    if (value < 0)
        value = 0;
    temperature = (value * (3300.0f / 4095.0f)) / 10; /* converte a leitura para a temperatura em °C*/
}