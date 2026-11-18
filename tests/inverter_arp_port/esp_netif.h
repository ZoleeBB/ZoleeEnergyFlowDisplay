typedef struct esp_netif esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
int esp_netif_get_netif_impl_index(esp_netif_t *netif);
