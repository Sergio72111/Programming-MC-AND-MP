#include "stm32f10x.h"   // библиотека для работы с STM32
#include "ds18b20.h"     // библиотека для датчиков DS18B20
#include <stdio.h>       // для sprintf()

#define MAX_SENSORS 3
#define TEMP_ERROR_VALUE -1000.0f

// Массив для хранения данных трёх датчиков
Sensor sensors[MAX_SENSORS];



void USART2_Init(void)
{
    // Включение тактирования GPIOA, AFIO и USART2
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Настройка PA2 как USART2_TX
    GPIOA->CRL &= ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2);
    GPIOA->CRL |= GPIO_CRL_MODE2_1 | GPIO_CRL_MODE2_0;
    GPIOA->CRL |= GPIO_CRL_CNF2_1;

    // Скорость USART2 — 4800 бод
    USART2->BRR = 36000000 / 4800;

    // Включение передачи и USART2
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
}

// Отправка одного символа в терминал
void USART2_SendChar(char c)
{
    while (!(USART2->SR & USART_SR_TXE));
    USART2->DR = c;
}

// Отправка строки в терминал
void USART2_SendString(char *str)
{
    while (*str)
    {
        USART2_SendChar(*str++);
    }
}


// Проверка, пустой ли ROM-код датчика
uint8_t Is_ROM_Empty(uint8_t *rom)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (rom[i] != 0x00)
            return 0;
    }

    return 1;
}

// Сравнение двух ROM-кодов
uint8_t ROM_Equals(uint8_t *rom1, uint8_t *rom2)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (rom1[i] != rom2[i])
            return 0;
    }

    return 1;
}

// Очистка ROM-кода датчика
void Clear_ROM(uint8_t *rom)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
        rom[i] = 0x00;
}

// Начальная очистка массива датчиков
void Init_Sensors(void)
{
    uint8_t i, j;

    for (i = 0; i < MAX_SENSORS; i++)
    {
        sensors[i].raw_temp = 0;
        sensors[i].temp = TEMP_ERROR_VALUE;

        sensors[i].crc8_rom = 0;
        sensors[i].crc8_data = 0;
        sensors[i].crc8_rom_error = 0;
        sensors[i].crc8_data_error = 0;

        for (j = 0; j < 8; j++)
            sensors[i].ROM_code[j] = 0x00;

        for (j = 0; j < 9; j++)
            sensors[i].scratchpad_data[j] = 0x00;
    }
}



// Запись настроек TH, TL и CFG в датчик
void ds18b20_WriteScratchpad(uint8_t *rom, uint8_t th, uint8_t tl, uint8_t cfg)
{
    uint8_t i;

    ds18b20_Reset();

    ds18b20_WriteByte(0x55);   // выбор датчика по ROM-коду

    for (i = 0; i < 8; i++)
        ds18b20_WriteByte(rom[i]);

    ds18b20_WriteByte(0x4E);   // команда записи scratchpad
    ds18b20_WriteByte(th);
    ds18b20_WriteByte(tl);
    ds18b20_WriteByte(cfg);
}

// Сохранение настроек датчика
void ds18b20_CopyScratchpad(uint8_t *rom)
{
    uint8_t i;

    ds18b20_Reset();

    ds18b20_WriteByte(0x55);

    for (i = 0; i < 8; i++)
        ds18b20_WriteByte(rom[i]);

    ds18b20_WriteByte(0x48);   // команда копирования scratchpad

    DelayMicro(20000);
}



// Поиск, добавление и удаление датчиков
void Sensors_Update(uint8_t *devCount)
{
    Sensor found[MAX_SENSORS];

    uint8_t foundUsed[MAX_SENSORS] = {0, 0, 0};
    uint8_t foundHere[MAX_SENSORS] = {0, 0, 0};

    uint8_t newCount = 0;
    uint8_t i, j, k;
    uint8_t maxSlot = 0;

    // Поиск датчиков на шине 1-Wire
    newCount = Search_ROM(0xF0, found);

    // Проверка уже известных датчиков
    for (i = 0; i < MAX_SENSORS; i++)
    {
        if (!Is_ROM_Empty(sensors[i].ROM_code))
        {
            for (j = 0; j < newCount; j++)
            {
                if (!foundUsed[j] &&
                    ROM_Equals(sensors[i].ROM_code, found[j].ROM_code))
                {
                    foundUsed[j] = 1;
                    foundHere[i] = 1;
                    break;
                }
            }
        }
    }

    // Добавление новых датчиков в свободные слоты
    for (j = 0; j < newCount; j++)
    {
        if (!foundUsed[j])
        {
            for (i = 0; i < MAX_SENSORS; i++)
            {
                if (Is_ROM_Empty(sensors[i].ROM_code))
                {
                    for (k = 0; k < 8; k++)
                        sensors[i].ROM_code[k] = found[j].ROM_code[k];

                    foundUsed[j] = 1;
                    foundHere[i] = 1;
                    break;
                }
            }
        }
    }

    // Удаление отключённых датчиков
    for (i = 0; i < MAX_SENSORS; i++)
    {
        if (!Is_ROM_Empty(sensors[i].ROM_code) && !foundHere[i])
        {
            sensors[i].raw_temp = 0;
            sensors[i].temp = TEMP_ERROR_VALUE;
            sensors[i].crc8_data_error = 1;

            Clear_ROM(sensors[i].ROM_code);
        }
    }

    // Подсчёт активных датчиков
    *devCount = 0;

    for (i = 0; i < MAX_SENSORS; i++)
    {
        if (!Is_ROM_Empty(sensors[i].ROM_code))
            maxSlot = i + 1;
    }

    *devCount = maxSlot;

    // Настройка первого датчика
    if (*devCount > 0 && !Is_ROM_Empty(sensors[0].ROM_code) && foundHere[0])
    {
        ds18b20_WriteScratchpad(sensors[0].ROM_code, 34, 0, 0x3F);
        ds18b20_CopyScratchpad(sensors[0].ROM_code);
    }

    // Настройка второго датчика
    if (*devCount > 1 && !Is_ROM_Empty(sensors[1].ROM_code) && foundHere[1])
    {
        ds18b20_WriteScratchpad(sensors[1].ROM_code, 34, 0, 0x5F);
        ds18b20_CopyScratchpad(sensors[1].ROM_code);
    }

    // Настройка третьего датчика
    if (*devCount > 2 && !Is_ROM_Empty(sensors[2].ROM_code) && foundHere[2])
    {
        ds18b20_WriteScratchpad(sensors[2].ROM_code, 34, 0, 0x7F);
        ds18b20_CopyScratchpad(sensors[2].ROM_code);
    }
}


int main(void)
{
    uint8_t devCount = 0;
    uint8_t i = 0;
    char buffer[100];

    // Настройка таймера для задержек
    SysTick_Config(SystemCoreClock / 1000000);

    // Инициализация датчиков и 1-Wire
    Init_Sensors();
    ds18b20_PortInit();

    // Инициализация USART2
    USART2_Init();
    USART2_SendString("START\r\n");

    // Первый поиск датчиков
    Sensors_Update(&devCount);

    while (1)
    {
        // Обновление списка датчиков
        Sensors_Update(&devCount);

        // Поочерёдный опрос активных датчиков
        for (i = 0; i < devCount; i++)
        {
            if (Is_ROM_Empty(sensors[i].ROM_code))
                continue;

            // Запуск измерения температуры
            ds18b20_ConvertTemp(1, sensors[i].ROM_code);

            // Ожидание завершения измерения
            DelayMicro(750000);

            // Чтение scratchpad датчика
            ds18b20_ReadStratchpad(1,
                                   sensors[i].scratchpad_data,
                                   sensors[i].ROM_code);

            // Расчёт CRC
            sensors[i].crc8_data =
                Compute_CRC8(sensors[i].scratchpad_data, 8);

            // Проверка корректности данных
            sensors[i].crc8_data_error =
                (Compute_CRC8(sensors[i].scratchpad_data, 9) == 0) ? 0 : 1;

            if (!sensors[i].crc8_data_error)
            {
                // Получение сырого значения температуры
                sensors[i].raw_temp =
                    ((uint16_t)sensors[i].scratchpad_data[1] << 8) |
                     sensors[i].scratchpad_data[0];

                // Перевод температуры в градусы Цельсия
                sensors[i].temp = sensors[i].raw_temp * 0.0625f;

                // Формирование строки с результатом
                sprintf(buffer,
                        "Sensor %d: %.2f C  TH: %d  TL: %d  CFG: 0x%02X\r\n",
                        i + 1,
                        sensors[i].temp,
                        (int8_t)sensors[i].scratchpad_data[2],
                        (int8_t)sensors[i].scratchpad_data[3],
                        sensors[i].scratchpad_data[4]);

                USART2_SendString(buffer);
            }
            else
            {
                // Обработка ошибки CRC
                sensors[i].raw_temp = 0;
                sensors[i].temp = TEMP_ERROR_VALUE;

                sprintf(buffer,
                        "Sensor %d: ERROR\r\n",
                        i + 1);

                USART2_SendString(buffer);
            }

            // Пауза между опросом датчиков
            DelayMicro(100000);
        }

        // Разделитель между циклами измерения
        USART2_SendString("------\r\n");

        // Пауза перед следующим циклом
        DelayMicro(100000);
    }
}