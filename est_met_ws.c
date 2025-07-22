#include <stdio.h>
#include <string.h> 
#include <math.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "lib/ws2812.pio.h" 
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "font.h"

// Definição dos botões e leds
#define BOTAO_A 5
#define BOTAO_B 6
#define LED_R 13
#define LED_B 12
#define LED_G 11
#define BUZZER_PIN 10 
#define IS_RGBW false
#define NUM_LEDS 25 
#define WS2812_PIN 7 

// Definições da interface I2C para os sensores 
#define I2C_PORT_SENSORES i2c0
#define I2C_SDA_SENSORES 0
#define I2C_SCL_SENSORES 1

// Definições da interface I2C para o Display OLED 
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISP_ADDRESS 0x3C

// Definições de Rede Wi-Fi
#define WIFI_SSID "bythesword [2.4GHz]" // Insira aqui o nome da sua rede Wi-Fi
#define WIFI_PASS "30317512" // Insira aqui a senha da sua rede

// Pressão ao nível do mar em Pascal
#define SEA_LEVEL_PRESSURE 101325.0

// Struct para armazenar os dados mais recentes dos sensores
typedef struct {
    float temperature_bmp;
    float temperature_aht;
    float humidity;
    float pressure;
    double altitude;
} weather_data_t;

weather_data_t latest_data = {0}; // Instância global para os dados

uint slice_buz; // Slice do buzzer
uint32_t led_buffer[NUM_LEDS];
volatile int tela_atual = 0;
static volatile uint32_t last_time = 0; // Armazena o tempo do último evento (em microssegundos)

// Struct para armazenar as configurações de alerta definidas via web
typedef struct {
    float temp_max;
    float temp_min;
    float hum_max;
    float offset_temp;
    float offset_hum;
} alert_config_t;

// Valores padrão para os alertas
alert_config_t alert_config = { 
    .temp_max = 35.0, 
    .temp_min = 5.0, 
    .hum_max = 70.0, 
    .offset_temp = 0.0,
    .offset_hum = 0.0
};

// Struct para gerenciar o estado de uma conexão HTTP
struct http_state {
    char response[8192];
    size_t len;
    size_t sent;
};

// CÓDIGO DA PÁGINA WEB
const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Estacao Meteorologica</title>"
    "<style>"
    "body{font-family:sans-serif;text-align:center;padding:20px;margin:0;background:#f0f7ff;color:#333}"
    "h1{color:#0056b3}h2{color:#0056b3;border-bottom:2px solid #eee;padding-bottom:10px}"
    ".container{max-width:700px;margin:auto;background:white;padding:20px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.1)}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin:20px 0}"
    ".metric{background:#e9f5ff;padding:15px;border-radius:8px}"
    ".metric-label{font-weight:bold;color:#555;display:block;font-size:16px}"
    ".metric-value{font-size:24px;font-weight:bold;color:#007bff;margin-top:5px}"
    "form{margin-top:20px;display:flex;flex-direction:column;gap:15px;text-align:left}"
    "label{font-weight:bold}input[type='number']{padding:10px;border:1px solid #ccc;border-radius:5px;font-size:16px}"
    "button{font-size:18px;padding:12px 25px;border:none;border-radius:8px;background:#007bff;color:white;cursor:pointer;transition:background-color 0.3s}"
    "button:hover{background:#0056b3}"
    "canvas{margin-top:20px}"
    "</style>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
    "<script>"
    "let labels=[], dados_temp_aht=[], dados_temp_bmp=[], dados_umid=[], grafico;"
    "document.addEventListener('DOMContentLoaded', () => {"
    "  grafico = new Chart(document.getElementById('grafico').getContext('2d'), {"
    "    type: 'line',"
    "    data: {"
    "      labels: labels,"
    "      datasets: ["
    "        {label: 'Temperatura AHT20 (°C)', data: dados_temp_aht, borderColor: 'red', tension: 0.2},"
    "        {label: 'Temperatura BMP280 (°C)', data: dados_temp_bmp, borderColor: 'green', tension: 0.2},"
    "        {label: 'Umidade (%)', data: dados_umid, borderColor: 'blue', tension: 0.2}"
    "      ]"
    "    },"
    "    options: { responsive: true, scales: { y: { suggestedMin: 10, suggestedMax: 80 } } }"
    "  });"
    "  function atualizar() {"
    "    fetch('/estado').then(res => res.json()).then(data => {"
    "      document.getElementById('temp_bmp').innerText = data.temp_bmp.toFixed(2) + ' C';"
    "      document.getElementById('temp_aht').innerText = data.temp_aht.toFixed(2) + ' C';"
    "      document.getElementById('pressure').innerText = data.pressure.toFixed(3) + ' kPa';"
    "      document.getElementById('humidity').innerText = data.humidity.toFixed(2) + ' %';"
    "      document.getElementById('altitude').innerText = data.altitude.toFixed(2) + ' m';"
    "      if (document.activeElement.type !== 'number') {"
    "        document.getElementById('temp_min').value = data.temp_min;"
    "        document.getElementById('temp_max').value = data.temp_max;"
    "        document.getElementById('hum_max').value = data.hum_max;"
    "        document.getElementById('offset_temp').value = data.offset_temp;"
    "        document.getElementById('offset_hum').value = data.offset_hum;"
    "      }"
    "      const timestamp = new Date().toLocaleTimeString();"
    "      if (labels.length > 20) {"
    "        labels.shift(); dados_temp_aht.shift(); dados_temp_bmp.shift(); dados_umid.shift();"
    "      }"
    "      labels.push(timestamp);"
    "      dados_temp_aht.push(data.temp_aht);"
    "      dados_temp_bmp.push(data.temp_bmp);"
    "      dados_umid.push(data.humidity);"
    "      grafico.update();"
    "    });"
    "  }"
    "  setInterval(atualizar, 3000);"
    "  atualizar();"
    "  document.querySelector('form').addEventListener('submit', function(event) {"
    "    event.preventDefault();"
    "    const form_data = new URLSearchParams(new FormData(this)).toString();"
    "    fetch('/config', {"
    "      method: 'POST',"
    "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
    "      body: form_data"
    "    }).then(() => { alert('Configuracao salva!'); atualizar(); });"
    "  });"
    "});"
    "</script>"
    "</head><body>"
    "<div class='container'>"
    "<h1>Estacao Meteorologica WiFi</h1>"
    "<h2>Dados Atuais</h2>"
    "<div class='grid'>"
    "<div class='metric'><span class='metric-label'>Temperatura (BMP280)</span><span id='temp_bmp' class='metric-value'>--</span></div>"
    "<div class='metric'><span class='metric-label'>Temperatura (AHT20)</span><span id='temp_aht' class='metric-value'>--</span></div>"
    "<div class='metric'><span class='metric-label'>Umidade (AHT20)</span><span id='humidity' class='metric-value'>--</span></div>"
    "<div class='metric'><span class='metric-label'>Pressao</span><span id='pressure' class='metric-value'>--</span></div>"
    "<div class='metric' style='grid-column: span 2;'><span class='metric-label'>Altitude Estimada</span><span id='altitude' class='metric-value'>--</span></div>"
    "</div>"
    "<canvas id='grafico' width='600' height='200'></canvas>"
    "<form>"
    "<h2>Configurar Alertas</h2>"
    "<label for='temp_min'>Temperatura Minima (C):</label>"
    "<input type='number' id='temp_min' name='temp_min' step='0.1' required>"
    "<label for='temp_max'>Temperatura Maxima (C):</label>"
    "<input type='number' id='temp_max' name='temp_max' step='0.1' required>"
    "<label for='hum_max'>Umidade Maxima (%):</label>"
    "<input type='number' id='hum_max' name='hum_max' step='0.1' required>"
    "<label for='offset_temp'>Offset Temperatura (C):</label>"
    "<input type='number' id='offset_temp' name='offset_temp' step='0.1' required>"
    "<label for='offset_hum'>Offset Umidade (%):</label>"
    "<input type='number' id='offset_hum' name='offset_hum' step='0.1' required>"
    "<button type='submit'>Salvar</button>"
    "</form>"
    "</div></body></html>";


// Protótipos
void gpio_setup();
void WS2812_setup(); 
int get_led_index(int linha, int coluna);
void atualizar_matriz(float temp_aht, float temp_bmp, float hum);
void gpio_irq_handler(uint gpio, uint32_t events);
double calculate_altitude(double pressure);
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err);
static void start_http_server(void);

// Função principal
int main() {
    stdio_init_all();
    gpio_setup();
    WS2812_setup();

    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Inicialização do display OLED
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISP_ADDRESS, I2C_PORT_DISP);
    ssd1306_config(&ssd);

    // Escreve mensagem inicial no display
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 10);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 25);
    ssd1306_send_data(&ssd);

    // Inicialização do WIFI
    if (cyw43_arch_init()) {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 15000)) {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    // Exibe o IP no display após conectar
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str),"%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 16);
    ssd1306_send_data(&ssd);
    sleep_ms(2000); // Mostra o IP por 2 segundos

    // Inicialização dos sensores
    i2c_init(I2C_PORT_SENSORES, 400 * 1000);
    gpio_set_function(I2C_SDA_SENSORES, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_SENSORES, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_SENSORES);
    gpio_pull_up(I2C_SCL_SENSORES);

    // Inicializa o BMP280
    bmp280_init(I2C_PORT_SENSORES);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT_SENSORES, &params);

    // Inicializa o AHT20
    aht20_reset(I2C_PORT_SENSORES);
    aht20_init(I2C_PORT_SENSORES);

    // Inicializa o WebServer
    start_http_server(); 
    printf("%s\n", ip_str);

    // Buffers para formatar os dados para o display
    char str_temp[16], str_hum[16], str_press[16], str_alt[16], // Medições
        temp_max[16], temp_min[16], hum_max[16], temp_offset[16], hum_offset[16]; // Limites e calibrações

    while (1) {
        cyw43_arch_poll(); // Mantém a conexão Wi-Fi ativa

        // Lê os sensores
        int32_t raw_temp_bmp, raw_pressure;
        bmp280_read_raw(I2C_PORT_SENSORES, &raw_temp_bmp, &raw_pressure);
        
        // Converte os valores brutos para valores legíveis
        latest_data.temperature_bmp = bmp280_convert_temp(raw_temp_bmp, &params) / 100.0;
        latest_data.pressure = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &params);
        latest_data.altitude = calculate_altitude(latest_data.pressure);

        AHT20_Data data_aht;
        if (aht20_read(I2C_PORT_SENSORES, &data_aht)) {
            latest_data.temperature_aht = data_aht.temperature;
            latest_data.humidity = data_aht.humidity;
        }

        latest_data.temperature_aht += alert_config.offset_temp;
        latest_data.temperature_bmp += alert_config.offset_temp;
        latest_data.humidity += alert_config.offset_hum;

        // Imprime os valores no console serial (para debug)
        printf("BMP280: Temp=%.2fC, Press=%.2fPa | AHT20: Temp=%.2fC, Hum=%.2f%% | Alt=%.2fm\n",
               latest_data.temperature_bmp, latest_data.pressure,
               latest_data.temperature_aht, latest_data.humidity,
               latest_data.altitude);

        // Formata as strings para exibição
        snprintf(str_temp, sizeof(str_temp), "T:%.1fC", latest_data.temperature_aht);
        snprintf(str_hum, sizeof(str_hum), "U:%.0f%%", latest_data.humidity);
        snprintf(str_press, sizeof(str_press), "P:%.0fkPa", latest_data.pressure / 1000.0);
        snprintf(str_alt, sizeof(str_alt), "H:%.0fm", latest_data.altitude);
        snprintf(temp_max, sizeof(temp_max), "Tmax:%.0fC", alert_config.temp_max);
        snprintf(temp_min, sizeof(temp_min), "Tmin:%.0fC", alert_config.temp_min);
        snprintf(hum_max, sizeof(hum_max), "Umax:%.0f%", alert_config.hum_max);
        snprintf(temp_offset, sizeof(temp_offset), "Tofs:%.0fC", alert_config.offset_temp);
        snprintf(hum_offset, sizeof(hum_offset), "Uofs:%.0f%", alert_config.offset_hum);

        // Limpa e redesenha o display
        if(tela_atual == 0){
            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, "Est. Met.", 20, 2);
            ssd1306_line(&ssd, 0, 13, 127, 13, true);
            ssd1306_vline(&ssd, 64, 14, 64, true);
            ssd1306_draw_string(&ssd, "AHT20", 1, 17);
            ssd1306_draw_string(&ssd, "BMP280", 66, 17);
            ssd1306_line(&ssd, 0, 28, 127, 28, true);
            ssd1306_draw_string(&ssd, str_temp, 1, 31);
            ssd1306_draw_string(&ssd, str_hum, 1, 42);
            ssd1306_draw_string(&ssd, str_press, 66, 31);
            ssd1306_draw_string(&ssd, str_alt, 66, 42);
            ssd1306_send_data(&ssd);
        } else {
            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, "Est. Met.", 20, 2);
            ssd1306_line(&ssd, 0, 13, 127, 13, true);
            ssd1306_draw_string(&ssd, "Limite e Offset", 1, 17);
            ssd1306_line(&ssd, 0, 28, 127, 28, true);
            ssd1306_draw_string(&ssd, temp_max, 1, 31);
            ssd1306_draw_string(&ssd, temp_min, 1, 42);
            ssd1306_draw_string(&ssd, hum_max, 1, 53);
            ssd1306_draw_string(&ssd, temp_offset, 66, 31);
            ssd1306_draw_string(&ssd, hum_offset, 66, 42);
            ssd1306_send_data(&ssd);
        }
        
        // Configuração dos LEDS
        if(latest_data.temperature_aht > alert_config.temp_max || latest_data.temperature_bmp > alert_config.temp_max){
            gpio_put(LED_R, 1);
            gpio_put(LED_G, 0);
            gpio_put(LED_B, 0);
        } else if (latest_data.temperature_aht < alert_config.temp_min || latest_data.temperature_bmp < alert_config.temp_min){
            gpio_put(LED_R, 0);
            gpio_put(LED_G, 0);
            gpio_put(LED_B, 1);
        } else {
            gpio_put(LED_R, 0);
            gpio_put(LED_G, 1);
            gpio_put(LED_B, 0);
        }

        // Configuração do buzzer
        if (latest_data.humidity > alert_config.hum_max) {
            pwm_set_gpio_level(BUZZER_PIN, 60);
        } else {
            pwm_set_gpio_level(BUZZER_PIN, 0);
        }
        
        // Chamada da função que atualiza a matriz de leds
        atualizar_matriz(latest_data.temperature_aht, latest_data.temperature_bmp, latest_data.humidity);

        sleep_ms(500);
    }
    return 0;
}

void gpio_setup(){
    // LEDS
    gpio_init(LED_R);
    gpio_set_dir(LED_R, GPIO_OUT);
    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);
    gpio_init(LED_B);
    gpio_set_dir(LED_B, GPIO_OUT);
    //BOTOES
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);    
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    //buzzer
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    slice_buz = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_clkdiv(slice_buz, 40);
    pwm_set_wrap(slice_buz, 12500);
    pwm_set_enabled(slice_buz, true);  
}

void WS2812_setup(){
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);    
}

int get_led_index(int linha, int coluna) {
    int base = linha * 5;
    if (linha % 2 == 0) {
        return base + coluna;
    } else {
        return base + (4 - coluna);
    }
}

void atualizar_matriz(float temp_aht, float temp_bmp, float hum) {
    // limpa a matriz de leds
    for (int i = 0; i < NUM_LEDS; i++) led_buffer[i] = 0;

    // Cada LED representa 20%
    int leds_umid = (int)(hum / 20.0f);

    // Cada LED representa 10°C a partir de -10°C
    int leds_aht = (int)((temp_aht + 10.0f) / 10.0f);
    int leds_bmp = (int)((temp_bmp + 10.0f) / 10.0f);

    // Coluna 0 - vermelho (temp AHT)
    for (int i = 0; i < leds_aht; i++) {
        int index = get_led_index(i, 4);
        led_buffer[index] = 0x001000; 
    }

    // Coluna 2 - verde (temp BMP)
    for (int i = 0; i < leds_bmp; i++) {
        int index = get_led_index(i, 2);
        led_buffer[index] = 0x100000; 
    }

    // Coluna 4 - azul (umidade)
    for (int i = 0; i < leds_umid; i++) {
        int index = get_led_index(i, 0);
        led_buffer[index] = 0x000010; 
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio0, 0, led_buffer[i] << 8u);
    }
}

// Função de interrupção do GPIO para o modo BOOTSEL
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time > 200000) { // 200ms de debounce
        last_time = current_time;

        if (gpio == BOTAO_A) {       
            tela_atual = !tela_atual;

        } else if (gpio == BOTAO_B) {
            reset_usb_boot(0, 0);
        }
    }
}

// Função para calcular a altitude a partir da pressão atmosférica
double calculate_altitude(double pressure) {
    return 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));
}

// Função chamada quando os dados são enviados com sucesso
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len) {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

// Função principal que processa as requisições HTTP
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs) {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    // --- Endpoint para receber a configuração de alertas (POST /config) ---
    if (strstr(req, "POST /config")) {
        char *body = strstr(req, "\r\n\r\n"); // Encontra o início do corpo da requisição
        if (body) {
            body += 4; // Pula os cabeçalhos
            float new_temp_min, new_temp_max, new_hum_max, new_offset_temp, new_offset_hum;
            // Extrai os valores do corpo do POST (ex: "temp_min=5&temp_max=35&hum_max=70")
            if (sscanf(body, "temp_min=%f&temp_max=%f&hum_max=%f&offset_temp=%f&offset_hum=%f", 
                &new_temp_min, &new_temp_max, &new_hum_max, &new_offset_temp, &new_offset_hum) == 5) {
                // Atualiza a estrutura de configuração global com os novos valores
                alert_config.temp_min = new_temp_min;
                alert_config.temp_max = new_temp_max;
                alert_config.hum_max = new_hum_max;
                alert_config.offset_temp = new_offset_temp;
                alert_config.offset_hum = new_offset_hum;
                printf("Configuracao de alerta atualizada!\n");
                
                // Alerta de atualização de parâmetros
                for (int i = 0; i < 2; i++) {
                    pwm_set_gpio_level(BUZZER_PIN, 60);
                    sleep_ms(100);
                    pwm_set_gpio_level(BUZZER_PIN, 0);
                    sleep_ms(100);
                }
            }
        }
        // Responde com "200 OK" para confirmar o recebimento
        hs->len = snprintf(hs->response, sizeof(hs->response), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
    }
    // --- Endpoint para enviar o estado atual dos sensores em JSON (GET /estado) ---
    else if (strstr(req, "GET /estado")) {
        char json_payload[256];
        // Monta a string JSON usando os dados da estrutura global 'latest_data' e 'alert_config'
        int json_len = snprintf(json_payload, sizeof(json_payload),
            "{\"temp_bmp\":%.2f,\"temp_aht\":%.2f,\"humidity\":%.2f,\"pressure\":%.3f,\"altitude\":%.2f,"
            "\"temp_min\":%.1f,\"temp_max\":%.1f,\"hum_max\":%.1f,\"offset_temp\":%.1f,\"offset_hum\":%.1f}",
            latest_data.temperature_bmp, latest_data.temperature_aht, latest_data.humidity,
            latest_data.pressure / 1000.0, // Envia a pressão em kPa para facilitar a leitura
            latest_data.altitude,
            alert_config.temp_min,
            alert_config.temp_max, 
            alert_config.hum_max,
            alert_config.offset_temp, 
            alert_config.offset_hum
        );

        // Monta a resposta HTTP completa com o JSON no corpo
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                           json_len, json_payload);
    }
    // --- Serve a página HTML principal para qualquer outra requisição GET ---
    else {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    // Envia a resposta HTTP para o cliente
    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

// Callback para aceitar novas conexões
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Inicializa o servidor TCP na porta 80
static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb || tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao iniciar servidor TCP.\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP iniciado. Acesse o IP da placa no navegador.\n");
    
}