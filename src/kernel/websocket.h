#ifndef WEBSOCKET_H
#define WEBSOCKET_H
/* websocket.h — Fondasi AN: WebSocket client API */

/* Buka koneksi WebSocket ke url dan masuk mode interaktif.
 * url format: "ws://host/path" atau "wss://host/path"
 * Keluar dengan mengetik \q lalu Enter, atau saat server tutup. */
void ws_connect(const char *url);

#endif /* WEBSOCKET_H */
