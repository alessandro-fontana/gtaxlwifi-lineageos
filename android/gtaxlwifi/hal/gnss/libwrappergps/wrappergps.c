/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The RIL wrapper gpsd links to. On Samsung devices with a modem it reaches
 * the radio through libsecril-client; a Wi-Fi only device has no radio, and
 * the library Samsung ships for those does nothing: the two open calls hand
 * out an 8-byte client handle, every other call reports success. This is the
 * same behaviour, from source.
 */
#include <stdlib.h>

void *wrapperOpenClient_RILD(void)
{
	return malloc(8);
}

void *wrapperOpenClient_RILD_second(void)
{
	return malloc(8);
}

int wrapperCloseClient_RILD(void *client)
{
	(void)client;
	return 0;
}

int wrapperConnect_RILD(void *client)
{
	(void)client;
	return 0;
}

int wrapperConnect_RILD_second(void *client)
{
	(void)client;
	return 0;
}

int wrapperisConnected_RILD(void *client)
{
	(void)client;
	return 0;
}

int wrapperDisconnect_RILD(void *client)
{
	(void)client;
	return 0;
}

int wrapperRegisterUnsolicitedHandler(void *client, unsigned int id, void *handler)
{
	(void)client;
	(void)id;
	(void)handler;
	return 0;
}

int wrapperRegisterRequestCompleteHandler(void *client, unsigned int id, void *handler)
{
	(void)client;
	(void)id;
	(void)handler;
	return 0;
}

int wrapperRegisterErrorCallback(void *client, void *callback, void *data)
{
	(void)client;
	(void)callback;
	(void)data;
	return 0;
}

int wrapperInvokeOemRequestHookRaw(void *client, char *data, size_t len)
{
	(void)client;
	(void)data;
	(void)len;
	return 0;
}

int OemRequestHookRawLog(void *client, char *data)
{
	(void)client;
	(void)data;
	return 0;
}
