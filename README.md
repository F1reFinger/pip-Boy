## 🚧 PIP-BOY Display and Control Example 🚧

**Projeto em Desenvolvimento - Apenas para Fins de Teste e Demonstração**

Este projeto demonstra a inicialização e controle de um display **ST7789** usando um microcontrolador **ESP32** com interação via **Rotary Encoder** (Encoder Rotativo), simulando uma interface de estilo *Pip-Boy*.

| Supported Targets | ESP32 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- |

### 🚀 Como Usar o Exemplo

Siga as instruções detalhadas de inicialização do seu chip ESP32 e, em seguida, configure os pinos GPIO para o display ST7789 e o encoder rotativo.

1.  **Instalação do ESP-IDF:** Siga o guia de inicialização específico para sua placa e sistema operacional:
    * [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
2.  **Configuração de Pinagem (menuconfig):**
    * Execute `idf.py menuconfig`.
    * Configure os pinos GPIO corretos para as interfaces **SPI** (para o ST7789) e os pinos de **entrada** (para o Encoder).
3.  **Compilação e Flash:**
    * Compile: `idf.py build`
    * Flash: `idf.py -p PORT flash`
    * Monitore: `idf.py -p PORT monitor` (para ver logs de inicialização e eventos do encoder).

### 📂 Conteúdo da Pasta do Projeto

O projeto **pipboy** contém a lógica principal para inicializar o display e monitorar a entrada do usuário.