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
 *     names of its contributors may be used to endorse or promote software
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
 *   SoC-to-SoC UDP throughput probe, exposed as the `perf` OpenThread CLI command.
 *
 *   The file is linked as an OpenThread CLI vendor extension
 *   (`OPENTHREAD_CONFIG_CLI_VENDOR_COMMANDS_ENABLE`), so the identical source builds into
 *   `ot-cli-ftd` on this bare-metal port and into the nRF Connect SDK CLI sample. Only public
 *   OpenThread APIs are used, therefore what the measurement compares is the stack underneath
 *   (radio driver, RTOS or no RTOS), not two different test tools.
 *
 *   Concurrency: every entry point runs with the OpenThread stack owned by the caller - CLI
 *   commands and the UDP receive callback are already serialized by OpenThread itself, and
 *   `otPerfProcess()` is invoked from the platform hook below. No extra locking is needed.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openthread/cli.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/message.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/time.h>
#include <openthread/udp.h>

#include "ot_perf_drain_port.h"
#include "ot_perf_timing.h"

#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include <zephyr/net/openthread.h>
#endif

/*
 * `perf cca` reads the radio driver's own CCA accounting. OpenThread's `TxErrCca` is not a
 * substitute: with OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE=0 the CSMA-CA rounds run
 * inside nrf_802154, which raises the backoff exponent and retries on its own and only reports
 * failure to OpenThread after exhausting NRF_802154_CSMA_CA_MAX_BACKOFFS_DEFAULT rounds. A frame
 * that loses CCA once and then succeeds costs air time but leaves `TxErrCca` at zero.
 * `cca_failed_attempts` counts every attempt, retried or not.
 *
 * Zephyr/NCS already has the full driver headers on the include path. Bare metal does not, and
 * pulling in nrf_802154.h there drags nrfx HAL headers in without the right context. The stat
 * counter layout lives in nrf_802154_types.h; SERIALIZATION_HOST skips the nrf_radio.h include.
 */
#ifdef __ZEPHYR__
#include <nrf_802154.h>
#define OT_PERF_NRF_802154_STATS 1
#else
#define NRF_802154_SERIALIZATION_HOST 1
#include <nrf_802154_types.h>

void nrf_802154_stat_counters_get(nrf_802154_stat_counters_t *pStatCounters);
void nrf_802154_stat_counters_reset(void);

#define OT_PERF_NRF_802154_STATS 1
#endif

/**
 * Wire format, little endian, identical on both platforms:
 *
 *   0    magic   u32   'OTFP', filters out foreign traffic on the port
 *   4    seq     u32   0-based, data packets only; the FIN packet carries `total`
 *   8    txMs    u32   sender's `otPlatAlarmMilliGetNow()` at hand-off to the stack
 *   12   flags   u16   OT_PERF_FLAG_FIN
 *   14   rsvd    u16
 *   16   total   u32   number of data packets in this run, so the receiver knows the loss base
 */
#define OT_PERF_MAGIC 0x4f544650u
#define OT_PERF_HDR_SIZE 20
#define OT_PERF_FLAG_FIN 0x0001u

/* IPv6 MTU (1280) minus the IPv6 and UDP headers: the largest unfragmented UDP payload. */
#define OT_PERF_MAX_SIZE 1232
#define OT_PERF_DEFAULT_PORT 5001

/*
 * API 465 split OT_NETIF_THREAD into OT_NETIF_THREAD_HOST and OT_NETIF_THREAD_INTERNAL. Both
 * spellings select OpenThread's own UDP implementation rather than a platform UDP offload, which
 * is what keeps the data path identical on the two builds (NCS ships API 440, upstream is at 610).
 */
#if OPENTHREAD_API_VERSION >= 465
#define OT_PERF_NETIF OT_NETIF_THREAD_INTERNAL
#else
#define OT_PERF_NETIF OT_NETIF_THREAD
#endif

/* The FIN packet is repeated so that a single loss does not leave the receiver hanging. */
#define OT_PERF_FIN_REPEATS 3
#define OT_PERF_FIN_GAP_MS 25

/* A receiver that saw traffic but neither FIN nor a new packet for this long declares the run over. */
#define OT_PERF_IDLE_MS 2000

/* No datagrams at all for this long → treat the run as finished (lost FIN / black-hole UDP). */
#define OT_PERF_SERVER_EMPTY_MS (45u * 1000u)

/* Packets handed to the stack per pump tick; the rest is left to the next iteration so that the
 * radio and the tasklets keep being serviced. Keep this at 1: on a cooperative main loop the pump
 * pass sits inside the loop period that also gates how promptly the radio driver is polled for
 * otPlatRadioTxDone, so every extra otUdpSend here is added to the inter-frame gap. One datagram
 * per pass is still far above the rate the link can drain. */
#define OT_PERF_BURST 1

#define OT_PERF_RUN_LIMIT_MS (10u * 60u * 1000u)

/* Paced runs: back off after NO_BUFS so the pump measures stack capacity, not a retry spin. */
#define OT_PERF_NOBUFS_BACKOFF_MS 3u

/* Keep timing active until the radio queue is quiet after the last FIN. */
#define OT_PERF_DRAIN_IDLE_MS 80u
#define OT_PERF_DRAIN_TIMEOUT_MS 5000u

typedef enum
{
    kPerfIdle = 0,
    kPerfRunning,
    kPerfFinishing,
    kPerfDraining,
    kPerfDone,
    kPerfStopped,
} PerfState;

typedef struct
{
    otUdpSocket mSocket;
    PerfState   mState;
    bool        mOpen;
    uint16_t    mPort;
    uint32_t    mTotal;
    uint32_t    mPackets;
    uint32_t    mBytes;
    uint32_t    mMaxSeq;
    uint32_t    mOutOfOrder;
    uint32_t    mDuplicates;
    uint32_t    mBadHeader;
    uint32_t    mFirstMs;
    uint32_t    mLastMs;
    uint32_t    mLastClientTxMs;
    uint32_t    mActivityMs;
    int32_t     mDelayMin;
    int32_t     mDelayMax;
    bool        mHaveDelay;
} PerfServer;

typedef struct
{
    otUdpSocket   mSocket;
    PerfState     mState;
    bool          mOpen;
    otMessageInfo mMessageInfo;
    uint16_t      mSize;
    uint32_t      mCount;
    uint32_t      mIntervalMs;
    uint32_t      mSent;
    uint32_t      mBytes;
    uint32_t      mNoBufs;
    uint32_t      mErrors;
    uint32_t      mStartMs;
    uint32_t      mFirstTxMs;
    uint32_t      mLastTxMs;
    uint8_t       mFinLeft;
    uint32_t      mFinMs;
    uint32_t      mDrainStartMs;
    uint32_t      mNoBufsBackoffUntilMs;
} PerfClient;

static PerfServer sServer;
static PerfClient sClient;

static const uint8_t sPadding[64] = {0};

void otPerfProcess(otInstance *aInstance);

/*
 * The single platform difference: something has to call otPerfProcess() while a client run is in
 * flight. Bare metal pumps it from the driver pass of the main loop (see system_nrf54.c);
 * Zephyr cannot be hooked that way, so a small thread does it under the OpenThread API lock.
 * The measurement logic below is shared verbatim.
 */
#ifdef __ZEPHYR__

#define OT_PERF_PUMP_STACK_SIZE 2048
#define OT_PERF_PUMP_PRIORITY 10

K_SEM_DEFINE(sPumpWake, 0, 1);

static otInstance *PerfGetInstance(void);

static void PumpThread(void *aArg1, void *aArg2, void *aArg3)
{
    ARG_UNUSED(aArg1);
    ARG_UNUSED(aArg2);
    ARG_UNUSED(aArg3);

    while (true)
    {
        k_sem_take(&sPumpWake, K_FOREVER);

        while (sClient.mState == kPerfRunning || sClient.mState == kPerfFinishing || sClient.mState == kPerfDraining)
        {
            uint64_t startUs = otPlatTimeGet();

            openthread_mutex_lock();
            otPerfProcess(PerfGetInstance());
            openthread_mutex_unlock();

            otPerfTimingAddPumpThreadUs((uint32_t)(otPlatTimeGet() - startUs));
            k_msleep(1);
        }
    }
}

K_THREAD_DEFINE(sPumpTid,
                OT_PERF_PUMP_STACK_SIZE,
                PumpThread,
                NULL,
                NULL,
                NULL,
                OT_PERF_PUMP_PRIORITY,
                0,
                0);

static void PumpKick(void) { k_sem_give(&sPumpWake); }

#else

static void PumpKick(void) {}

#endif // __ZEPHYR__

/*
 * `otInstanceInitSingle()` is idempotent - it returns the already initialized single instance -
 * which makes it the one instance getter that reads the same on both platforms. Single-instance
 * builds only (OPENTHREAD_CONFIG_MULTIPLE_INSTANCE_ENABLE=0), which is the case for ot-cli-ftd
 * here and for the NCS CLI sample.
 */
static otInstance *PerfGetInstance(void) { return otInstanceInitSingle(); }

static void Encode16(uint8_t *aBuf, uint16_t aValue)
{
    aBuf[0] = (uint8_t)(aValue & 0xff);
    aBuf[1] = (uint8_t)(aValue >> 8);
}

static void Encode32(uint8_t *aBuf, uint32_t aValue)
{
    aBuf[0] = (uint8_t)(aValue & 0xff);
    aBuf[1] = (uint8_t)((aValue >> 8) & 0xff);
    aBuf[2] = (uint8_t)((aValue >> 16) & 0xff);
    aBuf[3] = (uint8_t)((aValue >> 24) & 0xff);
}

static uint16_t Decode16(const uint8_t *aBuf) { return (uint16_t)(aBuf[0] | ((uint16_t)aBuf[1] << 8)); }

static uint32_t Decode32(const uint8_t *aBuf)
{
    return (uint32_t)aBuf[0] | ((uint32_t)aBuf[1] << 8) | ((uint32_t)aBuf[2] << 16) | ((uint32_t)aBuf[3] << 24);
}

static const char *StateName(PerfState aState)
{
    switch (aState)
    {
    case kPerfIdle:
        return "idle";
    case kPerfRunning:
        return "running";
    case kPerfFinishing:
        return "finishing";
    case kPerfDraining:
        return "draining";
    case kPerfDone:
        return "done";
    case kPerfStopped:
        return "stopped";
    }

    return "unknown";
}

/* Rates are printed as fixed point so that the report needs no floating point support in printf. */
static void OutputRate(const char *aName, uint32_t aUnits, uint32_t aScale, uint32_t aDurationMs)
{
    uint32_t value = 0;

    if (aDurationMs != 0)
    {
        value = (uint32_t)(((uint64_t)aUnits * aScale) / aDurationMs);
    }

    otCliOutputFormat(" %s=%u.%02u", aName, (unsigned)(value / 100), (unsigned)(value % 100));
}

//---------------------------------------------------------------------------------------------------------------------
// Server

static uint32_t ServerDurationMs(void)
{
    uint32_t span;

    if (sServer.mPackets == 0)
    {
        return 0;
    }

    span = (sServer.mLastMs >= sServer.mFirstMs) ? (sServer.mLastMs - sServer.mFirstMs) : 0;

    return span ? span : 1u;
}

static void ServerResetStats(void)
{
    uint16_t    port  = sServer.mPort;
    bool        open  = sServer.mOpen;
    PerfState   state = sServer.mOpen ? kPerfRunning : kPerfIdle;
    otUdpSocket socket;

    socket = sServer.mSocket;
    memset(&sServer, 0, sizeof(sServer));
    sServer.mSocket = socket;
    sServer.mPort   = port;
    sServer.mOpen   = open;
    sServer.mState  = state;
}

static void HandleServerReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t  header[OT_PERF_HDR_SIZE];
    uint32_t now = otPlatAlarmMilliGetNow();
    uint16_t length;
    uint32_t seq;
    uint32_t txMs;
    uint16_t flags;
    int32_t  delay;

    (void)aContext;
    (void)aMessageInfo;

    length = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);

    if (length < OT_PERF_HDR_SIZE ||
        otMessageRead(aMessage, otMessageGetOffset(aMessage), header, sizeof(header)) != sizeof(header) ||
        Decode32(&header[0]) != OT_PERF_MAGIC)
    {
        sServer.mBadHeader++;
        return;
    }

    seq            = Decode32(&header[4]);
    txMs           = Decode32(&header[8]);
    flags          = Decode16(&header[12]);
    sServer.mTotal = Decode32(&header[16]);

    if (flags & OT_PERF_FLAG_FIN)
    {
        sServer.mActivityMs = now;

        if (sServer.mState == kPerfRunning)
        {
            sServer.mState = kPerfDone;
        }
        else
        {
            sServer.mDuplicates++;
        }

        return;
    }

    if (sServer.mPackets == 0)
    {
        sServer.mFirstMs = now;
        sServer.mState   = kPerfRunning;
    }

    sServer.mPackets++;
    sServer.mBytes += length;
    sServer.mLastMs         = now;
    sServer.mLastClientTxMs = txMs;
    sServer.mActivityMs     = now;

    if (seq >= sServer.mMaxSeq)
    {
        sServer.mMaxSeq = seq;
    }
    else
    {
        sServer.mOutOfOrder++;
    }

    /* The two nodes have unsynchronized millisecond clocks, so the absolute value is meaningless;
     * the spread between min and max is not, and it is what one-way delay variation is made of. */
    delay = (int32_t)(now - txMs);

    if (!sServer.mHaveDelay)
    {
        sServer.mHaveDelay = true;
        sServer.mDelayMin  = delay;
        sServer.mDelayMax  = delay;
    }
    else if (delay < sServer.mDelayMin)
    {
        sServer.mDelayMin = delay;
    }
    else if (delay > sServer.mDelayMax)
    {
        sServer.mDelayMax = delay;
    }
}

static otError ServerStart(uint16_t aPort)
{
    otInstance *instance = PerfGetInstance();
    otSockAddr  sockAddr;
    otError     error;

    if (sServer.mOpen)
    {
        return OT_ERROR_ALREADY;
    }

    ServerResetStats();
    sServer.mPort = aPort;

    error = otUdpOpen(instance, &sServer.mSocket, HandleServerReceive, NULL);

    if (error != OT_ERROR_NONE)
    {
        return error;
    }

    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.mPort = aPort;

    error = otUdpBind(instance, &sServer.mSocket, &sockAddr, OT_PERF_NETIF);

    if (error != OT_ERROR_NONE)
    {
        (void)otUdpClose(instance, &sServer.mSocket);
        return error;
    }

    sServer.mOpen  = true;
    sServer.mState = kPerfRunning;
    sServer.mActivityMs = otPlatAlarmMilliGetNow();

    return OT_ERROR_NONE;
}

static otError ServerStop(void)
{
    if (!sServer.mOpen)
    {
        return OT_ERROR_INVALID_STATE;
    }

    (void)otUdpClose(PerfGetInstance(), &sServer.mSocket);
    sServer.mOpen = false;

    if (sServer.mState == kPerfRunning)
    {
        sServer.mState = sServer.mPackets != 0 ? kPerfDone : kPerfStopped;
    }

    return OT_ERROR_NONE;
}

static void ServerReport(void)
{
    uint32_t duration = ServerDurationMs();
    uint32_t expected = (sServer.mTotal != 0) ? sServer.mTotal : (sServer.mPackets != 0 ? sServer.mMaxSeq + 1 : 0);
    uint32_t lost     = (expected > sServer.mPackets) ? expected - sServer.mPackets : 0;
    int32_t  spread   = sServer.mHaveDelay ? sServer.mDelayMax - sServer.mDelayMin : 0;

    /* Idle: after last datagram when we saw traffic, or after a long empty wait when we did not. */
    if (sServer.mState == kPerfRunning)
    {
        uint32_t now = otPlatAlarmMilliGetNow();

        if (sServer.mPackets != 0 && (now - sServer.mLastMs) > OT_PERF_IDLE_MS)
        {
            sServer.mState = kPerfDone;
        }
        else if (sServer.mPackets == 0 && (now - sServer.mActivityMs) > OT_PERF_SERVER_EMPTY_MS)
        {
            sServer.mState = kPerfDone;
        }
    }

    uint32_t tailMs = 0;

    if (sServer.mPackets != 0 && sServer.mLastMs >= sServer.mLastClientTxMs)
    {
        tailMs = sServer.mLastMs - sServer.mLastClientTxMs;
    }

    otCliOutputFormat("perf: server state=%s port=%u pkts=%u bytes=%u expected=%u lost=%u ooo=%u dup=%u badhdr=%u "
                      "dur_ms=%u rx_first_ms=%u rx_last_ms=%u tail_ms=%u",
                      StateName(sServer.mState), (unsigned)sServer.mPort, (unsigned)sServer.mPackets,
                      (unsigned)sServer.mBytes, (unsigned)expected, (unsigned)lost, (unsigned)sServer.mOutOfOrder,
                      (unsigned)sServer.mDuplicates, (unsigned)sServer.mBadHeader, (unsigned)duration,
                      (unsigned)sServer.mFirstMs, (unsigned)sServer.mLastMs, (unsigned)tailMs);

    OutputRate("goodput_kbps", sServer.mBytes, 800, duration);
    OutputRate("pps", sServer.mPackets, 100000, duration);

    otCliOutputFormat(" spread_ms=%d\r\n", (int)spread);
}

//---------------------------------------------------------------------------------------------------------------------
// Client

static uint32_t ClientDurationMs(void)
{
    uint32_t span;

    if (sClient.mSent == 0)
    {
        return 0;
    }

    span = (sClient.mLastTxMs >= sClient.mFirstTxMs) ? (sClient.mLastTxMs - sClient.mFirstTxMs) : 0;

    if (span == 0)
    {
        span = otPlatAlarmMilliGetNow() - sClient.mStartMs;
    }

    return span ? span : 1u;
}

static otError ClientSendOne(uint32_t aSeq, uint16_t aFlags)
{
    otInstance       *instance = PerfGetInstance();
    otMessageSettings settings = {true, OT_MESSAGE_PRIORITY_NORMAL};
    uint8_t           header[OT_PERF_HDR_SIZE];
    otMessage        *message;
    otError           error;
    uint16_t          remaining;
    uint32_t          now = otPlatAlarmMilliGetNow();
    uint64_t          startUs;
    uint64_t          endUs;

    startUs = otPlatTimeGet();

    message = otUdpNewMessage(instance, &settings);

    if (message == NULL)
    {
        otPerfTimingAddSendOneUs((uint32_t)(otPlatTimeGet() - startUs));
        return OT_ERROR_NO_BUFS;
    }

    Encode32(&header[0], OT_PERF_MAGIC);
    Encode32(&header[4], aSeq);
    Encode32(&header[8], now);
    Encode16(&header[12], aFlags);
    Encode16(&header[14], 0);
    Encode32(&header[16], sClient.mCount);

    error = otMessageAppend(message, header, sizeof(header));

    remaining = (aFlags & OT_PERF_FLAG_FIN) ? 0 : (uint16_t)(sClient.mSize - OT_PERF_HDR_SIZE);

    while (error == OT_ERROR_NONE && remaining != 0)
    {
        uint16_t chunk = (remaining < sizeof(sPadding)) ? remaining : (uint16_t)sizeof(sPadding);

        error = otMessageAppend(message, sPadding, chunk);
        remaining = (uint16_t)(remaining - chunk);
    }

    if (error == OT_ERROR_NONE)
    {
        error = otUdpSend(instance, &sClient.mSocket, message, &sClient.mMessageInfo);
    }

    if (error != OT_ERROR_NONE)
    {
        otMessageFree(message);
    }

    endUs = otPlatTimeGet();
    otPerfTimingAddSendOneUs((uint32_t)(endUs - startUs));

    return error;
}

/* The socket is send-only, but OpenThread invokes the handler unconditionally, so it must exist. */
static void HandleClientReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    (void)aContext;
    (void)aMessage;
    (void)aMessageInfo;
}

static void ClientComplete(PerfState aState)
{
    if (sClient.mOpen)
    {
        (void)otUdpClose(PerfGetInstance(), &sClient.mSocket);
        sClient.mOpen = false;
    }

    sClient.mState = aState;

    if (aState == kPerfDone || aState == kPerfStopped)
    {
        otPerfTimingSetActive(false);
    }
}

static void ClientBeginDraining(void)
{
    sClient.mDrainStartMs = otPlatAlarmMilliGetNow();
    sClient.mState        = kPerfDraining;
}

static bool ClientRadioIdle(otInstance *aInstance)
{
    if (otPlatRadioGetState(aInstance) == OT_RADIO_STATE_TRANSMIT)
    {
        return false;
    }

    return !otPerfPlatformRadioPending();
}

static void ClientDrainProcess(otInstance *aInstance)
{
    uint32_t now        = otPlatAlarmMilliGetNow();
    uint32_t lastTxDone = otPerfTimingGetLastTxDoneAlarmMs();

    if ((now - sClient.mDrainStartMs) > OT_PERF_DRAIN_TIMEOUT_MS)
    {
        ClientComplete(kPerfDone);
        return;
    }

    if (!ClientRadioIdle(aInstance))
    {
        return;
    }

    if (lastTxDone != 0U && (now - lastTxDone) < OT_PERF_DRAIN_IDLE_MS)
    {
        return;
    }

    ClientComplete(kPerfDone);
}

static void ClientFinish(PerfState aState)
{
    if (aState == kPerfDone)
    {
        ClientBeginDraining();
        return;
    }

    ClientComplete(aState);
}

static otError ClientStart(const otIp6Address *aDest,
                           uint16_t            aPort,
                           uint16_t            aSize,
                           uint32_t            aCount,
                           uint32_t            aIntervalMs)
{
    otInstance *instance = PerfGetInstance();
    otSockAddr  sockAddr;
    otError     error;

    if (sClient.mState == kPerfRunning || sClient.mState == kPerfFinishing || sClient.mState == kPerfDraining)
    {
        return OT_ERROR_BUSY;
    }

    if (aSize < OT_PERF_HDR_SIZE || aSize > OT_PERF_MAX_SIZE || aCount == 0)
    {
        return OT_ERROR_INVALID_ARGS;
    }

    memset(&sClient, 0, sizeof(sClient));

    sClient.mSize                        = aSize;
    sClient.mCount                       = aCount;
    sClient.mIntervalMs                  = aIntervalMs;
    sClient.mMessageInfo.mPeerAddr       = *aDest;
    sClient.mMessageInfo.mPeerPort       = aPort;
    sClient.mMessageInfo.mHopLimit       = 0;
    sClient.mMessageInfo.mAllowZeroHopLimit = false;

    error = otUdpOpen(instance, &sClient.mSocket, HandleClientReceive, NULL);

    if (error != OT_ERROR_NONE)
    {
        return error;
    }

    /* Port 0 makes OpenThread pick an ephemeral one; binding pins the netif explicitly. */
    memset(&sockAddr, 0, sizeof(sockAddr));
    error = otUdpBind(instance, &sClient.mSocket, &sockAddr, OT_PERF_NETIF);

    if (error != OT_ERROR_NONE)
    {
        (void)otUdpClose(instance, &sClient.mSocket);
        return error;
    }

    sClient.mOpen    = true;
    sClient.mStartMs = otPlatAlarmMilliGetNow();
    sClient.mFinLeft = OT_PERF_FIN_REPEATS;
    sClient.mState   = kPerfRunning;

    otPerfTimingReset();
    otPerfTimingSetActive(true);

    PumpKick();

    return OT_ERROR_NONE;
}

static void ClientProcess(void)
{
    uint32_t now = otPlatAlarmMilliGetNow();
    uint32_t due;
    uint32_t burst;

    if (sClient.mNoBufsBackoffUntilMs != 0U && now < sClient.mNoBufsBackoffUntilMs)
    {
        return;
    }

    sClient.mNoBufsBackoffUntilMs = 0;

    if ((now - sClient.mStartMs) > OT_PERF_RUN_LIMIT_MS)
    {
        ClientFinish(kPerfStopped);
        return;
    }

    if (sClient.mState == kPerfFinishing)
    {
        if ((now - sClient.mFinMs) < OT_PERF_FIN_GAP_MS)
        {
            return;
        }

        if (ClientSendOne(sClient.mCount, OT_PERF_FLAG_FIN) == OT_ERROR_NONE)
        {
            sClient.mFinLeft--;
        }

        sClient.mFinMs = now;

        if (sClient.mFinLeft == 0)
        {
            ClientFinish(kPerfDone);
        }

        return;
    }

    if (sClient.mIntervalMs == 0)
    {
        /* No pacing: keep the message pool as full as the stack allows and let OT_ERROR_NO_BUFS
         * be the back pressure. This is the "how fast can this stack go" mode. */
        due = sClient.mCount - sClient.mSent;
    }
    else
    {
        uint32_t target = (now - sClient.mStartMs) / sClient.mIntervalMs + 1;

        if (target > sClient.mCount)
        {
            target = sClient.mCount;
        }

        due = (target > sClient.mSent) ? target - sClient.mSent : 0;
    }

    for (burst = 0; burst < OT_PERF_BURST && due != 0; burst++, due--)
    {
        otError error = ClientSendOne(sClient.mSent, 0);

        if (error == OT_ERROR_NO_BUFS)
        {
            sClient.mNoBufs++;

            if (sClient.mIntervalMs != 0U)
            {
                sClient.mNoBufsBackoffUntilMs = now + OT_PERF_NOBUFS_BACKOFF_MS;
            }

            break;
        }

        if (error != OT_ERROR_NONE)
        {
            sClient.mErrors++;
            break;
        }

        if (sClient.mSent == 0)
        {
            sClient.mFirstTxMs = now;
        }

        sClient.mSent++;
        sClient.mBytes += sClient.mSize;
        sClient.mLastTxMs = now;
    }

    if (sClient.mSent == sClient.mCount)
    {
        sClient.mState = kPerfFinishing;
        sClient.mFinMs = now - OT_PERF_FIN_GAP_MS;
    }
}

static void ClientReport(void)
{
    uint32_t duration = ClientDurationMs();

    otCliOutputFormat("perf: client state=%s port=%u size=%u count=%u interval_ms=%u sent=%u bytes=%u nobufs=%u "
                      "err=%u dur_ms=%u tx_first_ms=%u tx_last_ms=%u submit_span_ms=%u",
                      StateName(sClient.mState), (unsigned)sClient.mMessageInfo.mPeerPort, (unsigned)sClient.mSize,
                      (unsigned)sClient.mCount, (unsigned)sClient.mIntervalMs, (unsigned)sClient.mSent,
                      (unsigned)sClient.mBytes, (unsigned)sClient.mNoBufs, (unsigned)sClient.mErrors,
                      (unsigned)duration, (unsigned)sClient.mFirstTxMs, (unsigned)sClient.mLastTxMs,
                      (unsigned)duration);

    OutputRate("offered_kbps", sClient.mBytes, 800, duration);
    OutputRate("pps", sClient.mSent, 100000, duration);

    otCliOutputFormat("\r\n");
}

void otPerfProcess(otInstance *aInstance)
{
    if (sClient.mState == kPerfRunning || sClient.mState == kPerfFinishing)
    {
        ClientProcess();
    }
    else if (sClient.mState == kPerfDraining)
    {
        ClientDrainProcess(aInstance);
    }
}

//---------------------------------------------------------------------------------------------------------------------
// CLI

static void OutputUsage(void)
{
    otCliOutputFormat("perf server start [port]\r\n");
    otCliOutputFormat("perf server stop|report|reset\r\n");
    otCliOutputFormat("perf client start <ipv6> <port> <size> <count> [interval_ms]\r\n");
    otCliOutputFormat("perf client stop|report\r\n");
    otCliOutputFormat("perf cca [reset]\r\n");
    otCliOutputFormat("perf timing [reset]\r\n");
}

static bool ParseUint32(const char *aArg, uint32_t *aValue)
{
    char         *end;
    unsigned long value = strtoul(aArg, &end, 0);

    if (*aArg == '\0' || *end != '\0')
    {
        return false;
    }

    *aValue = (uint32_t)value;

    return true;
}

static otError ProcessServer(uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0)
    {
        ServerReport();
        return OT_ERROR_NONE;
    }

    if (strcmp(aArgs[0], "start") == 0)
    {
        uint32_t port = OT_PERF_DEFAULT_PORT;

        if (aArgsLength > 1 && (!ParseUint32(aArgs[1], &port) || port == 0 || port > UINT16_MAX))
        {
            return OT_ERROR_INVALID_ARGS;
        }

        return ServerStart((uint16_t)port);
    }

    if (strcmp(aArgs[0], "stop") == 0)
    {
        return ServerStop();
    }

    if (strcmp(aArgs[0], "report") == 0)
    {
        ServerReport();
        return OT_ERROR_NONE;
    }

    if (strcmp(aArgs[0], "reset") == 0)
    {
        ServerResetStats();
        return OT_ERROR_NONE;
    }

    return OT_ERROR_INVALID_COMMAND;
}

static otError ProcessClient(uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0)
    {
        ClientReport();
        return OT_ERROR_NONE;
    }

    if (strcmp(aArgs[0], "start") == 0)
    {
        otIp6Address dest;
        uint32_t     port;
        uint32_t     size;
        uint32_t     count;
        uint32_t     interval = 0;

        if (aArgsLength < 5)
        {
            return OT_ERROR_INVALID_ARGS;
        }

        if (otIp6AddressFromString(aArgs[1], &dest) != OT_ERROR_NONE)
        {
            return OT_ERROR_INVALID_ARGS;
        }

        if (!ParseUint32(aArgs[2], &port) || port == 0 || port > UINT16_MAX || !ParseUint32(aArgs[3], &size) ||
            !ParseUint32(aArgs[4], &count))
        {
            return OT_ERROR_INVALID_ARGS;
        }

        if (aArgsLength > 5 && !ParseUint32(aArgs[5], &interval))
        {
            return OT_ERROR_INVALID_ARGS;
        }

        return ClientStart(&dest, (uint16_t)port, (uint16_t)size, count, interval);
    }

    if (strcmp(aArgs[0], "stop") == 0)
    {
        if (sClient.mState != kPerfRunning && sClient.mState != kPerfFinishing && sClient.mState != kPerfDraining)
        {
            return OT_ERROR_INVALID_STATE;
        }

        ClientComplete(kPerfStopped);
        return OT_ERROR_NONE;
    }

    if (strcmp(aArgs[0], "report") == 0)
    {
        ClientReport();
        return OT_ERROR_NONE;
    }

    return OT_ERROR_INVALID_COMMAND;
}

static otError ProcessTiming(uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 1 && strcmp(aArgs[0], "reset") == 0)
    {
        otPerfTimingReset();
        return OT_ERROR_NONE;
    }

    if (aArgsLength != 0)
    {
        return OT_ERROR_INVALID_COMMAND;
    }

    otPerfTimingReport();
    return OT_ERROR_NONE;
}

static otError ProcessCca(uint8_t aArgsLength, char *aArgs[])
{
#ifdef OT_PERF_NRF_802154_STATS
    nrf_802154_stat_counters_t counters;

    if (aArgsLength == 1 && strcmp(aArgs[0], "reset") == 0)
    {
        nrf_802154_stat_counters_reset();
        return OT_ERROR_NONE;
    }

    if (aArgsLength != 0)
    {
        return OT_ERROR_INVALID_COMMAND;
    }

    nrf_802154_stat_counters_get(&counters);

    otCliOutputFormat("perf: cca failed_attempts=%u rx_frames=%u rx_energy=%u rx_preambles=%u\r\n",
                      (unsigned)counters.cca_failed_attempts, (unsigned)counters.received_frames,
                      (unsigned)counters.received_energy_events, (unsigned)counters.received_preambles);

    return OT_ERROR_NONE;
#else
    (void)aArgsLength;
    (void)aArgs;

    /* Keep the field shape so the harness parses a value instead of erroring out. */
    otCliOutputFormat("perf: cca failed_attempts=- rx_frames=- rx_energy=- rx_preambles=-\r\n");

    return OT_ERROR_NONE;
#endif
}

static otError ProcessPerf(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    (void)aContext;

    if (aArgsLength == 0 || strcmp(aArgs[0], "help") == 0)
    {
        OutputUsage();
        return OT_ERROR_NONE;
    }

    if (strcmp(aArgs[0], "server") == 0)
    {
        return ProcessServer((uint8_t)(aArgsLength - 1), &aArgs[1]);
    }

    if (strcmp(aArgs[0], "client") == 0)
    {
        return ProcessClient((uint8_t)(aArgsLength - 1), &aArgs[1]);
    }

    if (strcmp(aArgs[0], "cca") == 0)
    {
        return ProcessCca((uint8_t)(aArgsLength - 1), &aArgs[1]);
    }

    if (strcmp(aArgs[0], "timing") == 0)
    {
        return ProcessTiming((uint8_t)(aArgsLength - 1), &aArgs[1]);
    }

    return OT_ERROR_INVALID_COMMAND;
}

static const otCliCommand sPerfCommands[] = {
    {"perf", ProcessPerf},
};

void otCliVendorSetUserCommands(void)
{
    (void)otCliSetUserCommands(sPerfCommands, (uint8_t)(sizeof(sPerfCommands) / sizeof(sPerfCommands[0])), NULL);
}
