/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   Transport configuration for nRF54L15 DK bare-metal RCP (UART / Spinel).
 *
 *   Defaults aligned with NCS OpenThread coprocessor on nrf54l15dk:
 *   - uart20 / NRF_UARTE20, 1 Mbps, HWFC
 *   - TX=P1.4, RX=P1.5, RTS=P1.6, CTS=P1.7
 *     (nrf54l15dk pinctrl / openthread coprocessor overlay)
 *
 *   Alternative on the same DK (console / board_config.h style):
 *   - uart30 / NRF_UARTE30: P0.0 TX, P0.1 RX, P0.2 RTS, P0.3 CTS
 *
 *   Note: nrf54l15.cmake sets -DUART_BAUDRATE=NRF_UARTE_BAUDRATE_${OT_UART_BAUDRATE}
 *   (OT_UART_BAUDRATE cache default: 1000000). Override at configure time if needed.
 */

#ifndef TRANSPORT_CONFIG_H_
#define TRANSPORT_CONFIG_H_

#include "nrf.h"
#include "hal/nrf_spis.h"
#include "hal/nrf_uarte.h"

/*******************************************************************************
 * @section UART Driver Configuration (Spinel / OT RCP on nRF54L15 DK).
 ******************************************************************************/

#ifndef UART_INSTANCE
#define UART_INSTANCE NRF_UARTE20
#endif

#ifndef UART_PARITY
#define UART_PARITY NRF_UARTE_PARITY_EXCLUDED
#endif

#ifndef UART_HWFC_ENABLED
#define UART_HWFC_ENABLED 1
#endif

#ifndef UART_BAUDRATE
#define UART_BAUDRATE NRF_UARTE_BAUDRATE_1000000
#endif

#ifndef UART_IRQN
#define UART_IRQN UARTE20_IRQn
#endif

#ifndef UART_IRQ_HANDLER
#define UART_IRQ_HANDLER UARTE20_IRQHandler
#endif

#ifndef UART_IRQ_PRIORITY
#define UART_IRQ_PRIORITY 6
#endif

#ifndef UART_RX_BUFFER_SIZE
#define UART_RX_BUFFER_SIZE 512
#endif

/* nRF54L15 DK uart20 (PERI / port P1) — NCS zephyr,ot-uart = &uart20 */
#ifndef UART_PIN_TX
#define UART_PIN_TX NRF_GPIO_PIN_MAP(1, 4)
#endif

#ifndef UART_PIN_RX
#define UART_PIN_RX NRF_GPIO_PIN_MAP(1, 5)
#endif

#ifndef UART_PIN_RTS
#define UART_PIN_RTS NRF_GPIO_PIN_MAP(1, 6)
#endif

#ifndef UART_PIN_CTS
#define UART_PIN_CTS NRF_GPIO_PIN_MAP(1, 7)
#endif

/*******************************************************************************
 * @section SPI Slave configuration (not used in minimal UART RCP build).
 ******************************************************************************/

#ifndef SPIS_INSTANCE
#define SPIS_INSTANCE 0
#endif

#ifndef SPIS_MODE
#define SPIS_MODE NRF_SPIS_MODE_0
#endif

#ifndef SPIS_BIT_ORDER
#define SPIS_BIT_ORDER NRF_SPIS_BIT_ORDER_MSB_FIRST
#endif

#ifndef SPIS_IRQ_PRIORITY
#define SPIS_IRQ_PRIORITY 6
#endif

#ifndef SPIS_PIN_MOSI
#define SPIS_PIN_MOSI NRF_GPIO_PIN_MAP(1, 4)
#endif

#ifndef SPIS_PIN_MISO
#define SPIS_PIN_MISO NRF_GPIO_PIN_MAP(1, 5)
#endif

#ifndef SPIS_PIN_SCK
#define SPIS_PIN_SCK NRF_GPIO_PIN_MAP(1, 3)
#endif

#ifndef SPIS_PIN_CSN
#define SPIS_PIN_CSN NRF_GPIO_PIN_MAP(1, 6)
#endif

#ifndef SPIS_PIN_HOST_IRQ
#define SPIS_PIN_HOST_IRQ NRF_GPIO_PIN_MAP(1, 7)
#endif

/*******************************************************************************
 * @section USB driver configuration.
 ******************************************************************************/

#ifndef USB_HOST_UART_CONFIG_DELAY_MS
#define USB_HOST_UART_CONFIG_DELAY_MS 10
#endif

#ifndef USB_CDC_AS_SERIAL_TRANSPORT
#define USB_CDC_AS_SERIAL_TRANSPORT 0
#endif

#ifndef USB_CDC_ACM_COMM_INTERFACE
#define USB_CDC_ACM_COMM_INTERFACE 0
#endif

#ifndef USB_CDC_ACM_DATA_INTERFACE
#define USB_CDC_ACM_DATA_INTERFACE 1
#endif

#ifndef OPENTHREAD_PLATFORM_USE_PSEUDO_RESET
#define OPENTHREAD_PLATFORM_USE_PSEUDO_RESET 0
#endif

#endif // TRANSPORT_CONFIG_H_
