# Estação Meteorológica Wi-Fi com Interface Web

Este projeto consiste em uma estação meteorológica embarcada desenvolvida com o Raspberry Pi Pico W na plataforma BitDogLab. Ele monitora temperatura, umidade e pressão atmosférica utilizando sensores I2C e exibe os dados localmente e via interface web em tempo real.

---

## Conexões na BitDogLab

| Periférico        | GPIO / Interface |
|-------------------|------------------|
| AHT20             | I2C0             |
| BMP280            | I2C0             |
| Display OLED      | I2C1             |
| Matriz WS2812     | GPIO 7 (PIO)     |
| LED RGB           | GPIOs 11, 12, 13 |
| Buzzer            | GPIO 10 (PWM)    |
| Botão A           | GPIO 5           |
| Botão B           | GPIO 6           |

---

## Comportamento Visual

### Matriz WS2812:
- Coluna 0 (vermelho): Temperatura AHT20
- Coluna 2 (verde): Temperatura BMP280
- Coluna 4 (azul): Umidade

### LED RGB:
- Vermelho → Temperatura acima do máximo
- Azul → Temperatura abaixo do mínimo
- Verde → Dentro do intervalo seguro

### Buzzer:
- Bip curto ao salvar configuração
- Intermitente se a umidade ultrapassar o limite

---

## Interface Web

- Página HTML responsiva via AJAX
- Exibe dados ao vivo (texto + gráfico)
- Gráfico com três curvas: temp AHT, temp BMP e umidade
- Formulário para:
  - Alterar limites de alerta
  - Calibrar sensores com offset
- Atualização automática a cada 3 segundos

---

## Como usar

1. Conecte a BitDogLab ao PC 
2. Adicione o SSID e senha da rede Wi-Fi no código
3. Carregue o firmware `.uf2`
4. Acesse via navegador pelo IP local da placa
5. Visualize e configure tudo remotamente
6. Use botão A para alternar o display local

---

## Créditos

- Desenvolvido por Victor Gabriel
