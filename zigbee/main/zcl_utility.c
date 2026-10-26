#include "zcl_utility.h"

#include "esp_check.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_endpoint.h"

static const char *TAG = "zcl_utility";

esp_err_t esp_zcl_utility_add_ep_basic_manufacturer_info(esp_zb_ep_list_t *ep_list, uint8_t endpoint_id,
                                                         const zcl_basic_manufacturer_info_t *info)
{
    esp_zb_cluster_list_t *cluster_list;
    esp_zb_attribute_list_t *basic_cluster;

    ESP_RETURN_ON_FALSE(ep_list != NULL, ESP_ERR_INVALID_ARG, TAG, "Endpoint list is required");
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG, "Manufacturer info is required");
    ESP_RETURN_ON_FALSE(info->manufacturer_name != NULL, ESP_ERR_INVALID_ARG, TAG, "Manufacturer name is required");
    ESP_RETURN_ON_FALSE(info->model_identifier != NULL, ESP_ERR_INVALID_ARG, TAG, "Model identifier is required");

    cluster_list = esp_zb_ep_list_get_ep(ep_list, endpoint_id);
    ESP_RETURN_ON_FALSE(cluster_list != NULL, ESP_ERR_INVALID_ARG, TAG, "Endpoint %u not found", endpoint_id);

    basic_cluster = esp_zb_cluster_list_get_cluster(cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    ESP_RETURN_ON_FALSE(basic_cluster != NULL, ESP_ERR_INVALID_ARG, TAG, "Basic cluster missing on endpoint %u", endpoint_id);

    ESP_RETURN_ON_ERROR(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                                      (void *)info->manufacturer_name), TAG, "Failed to add manufacturer attribute");
    return esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                         (void *)info->model_identifier);
}
