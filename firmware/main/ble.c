#include "ble.h"

#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define DEVICE_NAME "TCG Proxy Card"

static const char *TAG = "ble";

// 7f0eca18-f2cb-47d8-a350-d535917badd7
static const ble_uuid128_t card_svc_uuid =
    BLE_UUID128_INIT(0xd7, 0xad, 0x9b, 0x17, 0x35, 0xd5, 0x50, 0xa3,
                     0xd8, 0x47, 0xcb, 0xf2, 0x18, 0xca, 0x0e, 0x7f);

// 46639c60-7777-461a-b888-68f51edd3f5d
static const ble_uuid128_t image_chr_uuid =
    BLE_UUID128_INIT(0x5d, 0x3f, 0xdd, 0x1e, 0xf5, 0x68, 0x88, 0xb8,
                     0x1a, 0x46, 0x77, 0x77, 0x60, 0x9c, 0x63, 0x46);

static uint8_t own_addr_type;

static void advertise(void);

static int image_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    ESP_LOGI(TAG, "image chunk: %u bytes", OS_MBUF_PKTLEN(ctxt->om));
    return 0;
}

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &card_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &image_chr_uuid.u,
                .access_cb = image_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {0},
        },
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect: status=%d", event->connect.status);
        if (event->connect.status != 0) {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect: reason=%d", event->disconnect.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    }
    return 0;
}

static void advertise(void)
{
    // Name and a 128-bit UUID don't both fit in the 31-byte advertising packet.
    struct ble_hs_adv_fields adv = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (uint8_t *)DEVICE_NAME,
        .name_len = sizeof(DEVICE_NAME) - 1,
        .name_is_complete = 1,
    };
    struct ble_hs_adv_fields scan_rsp = {
        .uuids128 = &card_svc_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };

    int rc = ble_gap_adv_set_fields(&adv);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_fields(&scan_rsp);
    }
    if (rc == 0) {
        rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising failed: rc=%d", rc);
    }
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable address: rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as \"%s\"", DEVICE_NAME);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset: reason=%d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t init_nvs(void)
{
    // The BLE controller stores calibration data in NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t ble_start(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init");
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_RETURN_ON_FALSE(ble_gatts_count_cfg(gatt_services) == 0, ESP_FAIL, TAG, "gatt count");
    ESP_RETURN_ON_FALSE(ble_gatts_add_svcs(gatt_services) == 0, ESP_FAIL, TAG, "gatt add");
    ESP_RETURN_ON_FALSE(ble_svc_gap_device_name_set(DEVICE_NAME) == 0, ESP_FAIL, TAG, "device name");

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
