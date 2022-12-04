/*
 * Program to control up to 16 relays from Pi Zero
 * Uses mosquitto and wiringPi libraries
 * MQTT message payload is 2 bytes, channel (1 - 16) and relay state (0 = off, any other value = on)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>

#include <mosquitto.h>
#include <wiringPi.h>

#define BUFFSIZE 50

static char g_Topic[BUFFSIZE] = {0};
static char g_Broker[BUFFSIZE] = {0};
static char g_WorkingDir[BUFFSIZE] = {0};
static char g_Title[] = "Pi Zero Relay Controller";


/* Callback called when the client receives a CONNACK message from the broker. */
void on_connect(struct mosquitto *mosq, void *obj, int reason_code)
{
	int rc;
	/* Print out the connection result. mosquitto_connack_string() produces an
	 * appropriate string for MQTT v3.x clients, the equivalent for MQTT v5.0
	 * clients is mosquitto_reason_string().
	 */
	printf("on_connect: %s\n", mosquitto_connack_string(reason_code));
	if(reason_code != 0)
	{
		/* If the connection fails for any reason, we don't want to keep on
		 * retrying in this example, so disconnect. Without this, the client
		 * will attempt to reconnect. */
		mosquitto_disconnect(mosq);
	}

	/* Making subscriptions in the on_connect() callback means that if the
	 * connection drops and is automatically resumed by the client, then the
	 * subscriptions will be recreated when the client reconnects. */
	rc = mosquitto_subscribe(mosq, NULL, g_Topic, 1);
	if(rc != MOSQ_ERR_SUCCESS)
	{
		fprintf(stderr, "Error subscribing: %s\n", mosquitto_strerror(rc));
		/* We might as well disconnect if we were unable to subscribe */
		mosquitto_disconnect(mosq);
	}
}


/* Callback called when the broker sends a SUBACK in response to a SUBSCRIBE. */
void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	int i;
	bool have_subscription = false;

	/* In this example we only subscribe to a single topic at once, but a
	 * SUBSCRIBE can contain many topics at once, so this is one way to check
	 * them all. */
	for(i=0; i<qos_count; i++)
	{
		printf("on_subscribe: %d:granted qos = %d\n", i, granted_qos[i]);
		if(granted_qos[i] <= 2){
			have_subscription = true;
		}
	}
	if(have_subscription == false)
	{
		/* The broker rejected all of our subscriptions, we know we only sent
		 * the one SUBSCRIBE, so there is no point remaining connected. */
		fprintf(stderr, "Error: All subscriptions rejected.\n");
		mosquitto_disconnect(mosq);
	}
}


/* Callback called when the client receives a message. */
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	char *pchar = (char *)msg->payload;
	int value;
	uint8_t *pin, *level;

	// display values received to console
	for (int i=0; i < msg->payloadlen; i++)
	{
		printf("[%d] ", *pchar);
		pchar++;
	}
	printf("\n");


	// check length of payload
	if (msg->payloadlen == 2)
	{
		pin = (uint8_t*)msg->payload;

		// check relay number in range
		if (*pin > 0 && *pin <= 16)
		{
			// use pins 0 - 15
			*pin -= 1;

			// second byte is state
			level = pin;
			level++;

			// set GPIO pin
			value = (*level == 0) ? HIGH : LOW;
			digitalWrite((int)(*pin), value);
		}
	}
}

/*
 * Display settings to console
 */
void displaySettings()
{
	printf("TOPIC: %s\n", g_Topic);
	printf("BROKER: %s\n\n", g_Broker);
}

/*
 * Read settings.dat file, set topic and broker
 */
bool readSettings()
{
	bool result = true;
	char buffer[BUFFSIZE];
	char filename[BUFFSIZE];
	int pos;
	char *pchar;

	// build full filename path
	strcpy(filename, g_WorkingDir);
	strcat(filename, "settings.dat");

	// open file
	FILE *fp = fopen(filename, "r");

	// set defaults if open fails
	if (fp == NULL)
	{
		printf("Unable to open file: %s, using defaults.\n\n", filename);
		strcpy(g_Topic, "relays");
		strcpy(g_Broker, "192.168.0.1");
		result = false;
	}
	else
	{
		while (fgets(buffer, BUFFSIZE, fp) != NULL)
		{

			// remove line feed and terminate
			pos = strlen(buffer) - 1;
			buffer[pos] = 0;

			if (strlen(buffer) > 0)
			{
				// ignore lines staring with #
				if (buffer[0] != '#')
				{
					pchar = strtok(buffer, " ");

					// make label upper case
					for (int i=0; i < strlen(pchar); i++)
						pchar[i] = toupper(pchar[i]);

					if (strcmp(pchar, "TOPIC") == 0)
					{
						pchar = strtok(NULL, " ");
						if (pchar != NULL) strcpy(g_Topic, pchar);
					}
					else if (strcmp(pchar, "BROKER") == 0)
					{
						pchar = strtok(NULL, " ");
						if (pchar != NULL) strcpy(g_Broker, pchar);
					}
				}
			}
		}

		// close file
		fclose(fp);
	}

	displaySettings();

	return result;
}

/*
 * Display program title to console
 */
void displayHeader()
{
	char buffer[BUFFSIZE];
	int len = strlen(g_Title);
	len += 4;

	memset(buffer, '#', len);
	buffer[len] = 0;
	printf("%s\n", buffer);

	strcpy(buffer, "# ");
	strcat(buffer, g_Title);
	strcat(buffer, " #");
	printf("%s\n", buffer);

	memset(buffer, '#', len);
	buffer[len] = 0;
	printf("%s\n\n", buffer);
}

/*
 * Parse working directory from argv[0]
 */
void parseWorkingDir(const char *path)
{
	char buffer[BUFFSIZE];
	char* pchar;
	int len;

	// copy exe path to local buffer
	strcpy(buffer, path);

	// find last occurence of /
	pchar = strrchr(buffer, '/');

	// if found, copy all before it to get path
	if (pchar != NULL)
	{
		len = pchar - buffer;
		strncpy(g_WorkingDir, buffer, len + 1);
	}
}


/*
 * Setup the GPIO pins
 * Uses wiringPi pins 0-15
 * All pins initially set high (relays off)
 */
void gpioSetup()
{
	if (wiringPiSetup() == 0)
	{
		for (int i=0; i < 16; i++)
		{
			pinMode(i, OUTPUT);
			digitalWrite(i, HIGH);
		}
	}
}



int main(int argc, char *argv[])
{
	struct mosquitto *mosq;
	int rc;

	// get current directory to open settings file
	parseWorkingDir(argv[0]);

	displayHeader();

	// read settings file
	readSettings();

	// setup GPIO pins
	gpioSetup();

	/* Required before calling other mosquitto functions */
	mosquitto_lib_init();

	/* Create a new client instance.
	 * id = NULL -> ask the broker to generate a client id for us
	 * clean session = true -> the broker should remove old sessions when we connect
	 * obj = NULL -> we aren't passing any of our private data for callbacks
	 */
	mosq = mosquitto_new(NULL, true, NULL);
	if(mosq == NULL)
	{
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* Configure callbacks. This should be done before connecting ideally. */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_message_callback_set(mosq, on_message);

	/* Connect to broker on port 1883, with a keepalive of 60 seconds.
	 * This call makes the socket connection only, it does not complete the MQTT
	 * CONNECT/CONNACK flow, you should use mosquitto_loop_start() or
	 * mosquitto_loop_forever() for processing net traffic. */
	rc = mosquitto_connect(mosq, g_Broker, 1883, 60);
	if(rc != MOSQ_ERR_SUCCESS)
	{
		mosquitto_destroy(mosq);
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		return 1;
	}

	/* Run the network loop in a blocking call. The only thing we do in this
	 * example is to print incoming messages, so a blocking call here is fine.
	 *
	 * This call will continue forever, carrying automatic reconnections if
	 * necessary, until the user calls mosquitto_disconnect().
	 */
	mosquitto_loop_forever(mosq, -1, 1);

	mosquitto_lib_cleanup();

	return 0;
}

