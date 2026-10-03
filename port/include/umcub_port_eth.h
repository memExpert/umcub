/*
 * Port capability: Ethernet MAC + generic IEEE 802.3 PHY over MDIO (RMII),
 * polled, whole frames (no FCS) in and out.
 */
#ifndef UMCUB_PORT_ETH_H
#define UMCUB_PORT_ETH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int umcub_port_eth_init(const uint8_t mac[6], unsigned phy_addr, const uint32_t *pins, unsigned npins);
void umcub_port_eth_deinit(void);
/* Polls the PHY (rate limited internally); true while the link is up. */
bool umcub_port_eth_link(void);
int umcub_port_eth_tx(const uint8_t *frame, size_t len);
/* Copies one received frame into buf; returns its length or 0. */
size_t umcub_port_eth_rx(uint8_t *buf, size_t max);

#endif
