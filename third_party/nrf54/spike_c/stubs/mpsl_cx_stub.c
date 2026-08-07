/*
 * Variant C spike: mpsl_cx stub — Thread-only, no Wi-Fi PTA.
 * Minimal types (avoid NCS mpsl headers in spike).
 */

#include <stddef.h>
#include <stdint.h>

typedef uint8_t mpsl_cx_op_map_t;
typedef uint8_t mpsl_cx_prio_t;
typedef uint8_t mpsl_cx_req_trig_t;

typedef struct
{
    mpsl_cx_op_map_t   ops;
    mpsl_cx_prio_t     prio;
    mpsl_cx_req_trig_t trigger;
} mpsl_cx_request_t;

typedef void (*mpsl_cx_cb_t)(mpsl_cx_op_map_t granted_ops);

#define MPSL_CX_OP_IDLE_LISTEN 0x01U
#define MPSL_CX_OP_RX          0x02U
#define MPSL_CX_OP_TX          0x04U

static mpsl_cx_cb_t     m_callback;
static mpsl_cx_op_map_t m_granted = MPSL_CX_OP_IDLE_LISTEN | MPSL_CX_OP_RX | MPSL_CX_OP_TX;

int32_t mpsl_cx_request(const mpsl_cx_request_t * p_req_params)
{
    (void)p_req_params;

    if (m_callback != NULL)
    {
        m_callback(m_granted);
    }

    return 0;
}

int32_t mpsl_cx_release(void)
{
    return 0;
}

int32_t mpsl_cx_granted_ops_get(mpsl_cx_op_map_t * p_granted_ops)
{
    if (p_granted_ops == NULL)
    {
        return -22;
    }

    *p_granted_ops = m_granted;
    return 0;
}

int32_t mpsl_cx_register_callback(mpsl_cx_cb_t cb)
{
    m_callback = cb;
    return 0;
}
