Environnements intelligents et communicants
=========================

> Farell Ahouandjinou
> Lucien Burdet

1. Objectif du projet
2. Principe du système domotique
3. ESP32 TTGO - T-display
    1. Configuration du projet
    2. BLE
        1. Xiaomi Mi Temperature and Humidity
        2. ESP32 BLE Arduino
    3. PubSubClient MQTT - Wifi
    4. TFT_eSPI Ecran
4. STM32
    1. MQTT
        1. Mise en place
        2. Connection au brooker
    2. ServoMoteur
5. Annexe
    1. MQTT Server Windows
    2. ESP32 TTGO - T-display

# Videos
Voici le lien de la vidéo du projet complet :
- https://youtu.be/0sxI_2qFv7I

Voici le lien de la video montrant la ESP32 en action :
- https://youtu.be/-RMJHuE7Nd0

# 1. Objectif du projet

Créer un mini-système domotique faisant intervenir différents capteurs, actionneurs et systèmes embarqués :
* Cartes à microcontrôleur STM32 et ESP32
* Capteur de température BLE
* Servomoteur

# 2. Principe du système domotique

* Un routeur Wi-Fi créé le réseau local de la maison
* Un module Wi-Fi ESP32 récupère la température/humidité via un thermomètre/hygromètre BLE et la transmet via MQTT
* Une carte à STM32, connectée en Ethernet au réseau, la récupère et commande en conséquence un volet de ventilation

# 3. ESP32 TTGO - T-display

Afin de programmer le microcontrôleur ESP32, nous avons utilisé ```VS Code``` avec l'extension ```PlatformIO IDE```. De plus, nous utilisons le framework ```arduino```.
Voici le lien de la video montrant la ESP32 en action :
- https://youtu.be/-RMJHuE7Nd0

## 3.1. Configuration du projet

Voici le configuration de notre projet :

```
[env:ttgo-lora32-v1]
platform = espressif32
board = ttgo-lora32-v1
framework = arduino
monitor_speed = 115200
board_build.partitions = huge_app.csv
lib_deps = 
	bodmer/TFT_eSPI@^2.3.70
	nkolban/ESP32 BLE Arduino@^1.0.1
	knolleary/PubSubClient@^2.8
```

Nous utilisons donc une carte ```board = ttgo-lora32-v1```, à savoir TTGO LoRa32-OLED V1.
Nous avons redéfinis la vitesse pour l'affichage dans le ternminal ```monitor_speed = 115200```.
Nous avons du changer le partitionnement de base car il était trop limité en place. En effet, nous utilisons des librairies qui sature assez rapidement la mémoire ```board_build.partitions = huge_app.csv```.
Enfin, les librairies que nous avons importés sont les suivantes :
- ```bodmer/TFT_eSPI``` qui nous permet d'afficher aisément sur l'écran OLED.
- ```nkolban/ESP32 BLE Arduino``` qui nous permet d'utiliser le bluetooth low energy.
- ```knolleary/PubSubClient``` qui nous permet d'utiliser MQTT.

## 3.2. BLE

### 3.2.1. Xiaomi Mi Temperature and Humidity
Le capteur Xiaomi Mi Temperature and Humidity envoie continuellement la température, l'humidité et la batterie sur des paquets d'advertising. En d'autre therme, le Mi Temp envoie à tout le monde (broadcast) cette trame périodiquement.
C'est un avantage pour nous car nous n'avons donc pas besoin de nous connecter en blutetooth au xiaomi.
En revanche, nous devons analyser le paquet d'advertising afin de récupérer correctement les données.

Après analyse et recherche sur internet, nous avons trouver que le paquet contient plusieurs objet que nous appellerons data. Il peut ne pas en contenir, en contenir qu'un ou en contenir plusieurs. Ces data se décomposent de la manière suivante :

| Emplacement    | Valeur     |
|----------------|------------|
| Byte 0         | type ID    |
| Byte 1	     | fixed 0x10 |
| Byte 2         | length     |
| Byte 3..3+len-1| data value |

Nous pouvons donc facilement les repérer en fonction de leur *type ID* et du *byte 1* qui vaut toujours la même valeur.
Voici la signification des différents *type ID* :
| Type ID | Type                   | Data                                                    |
|---------|------------------------|---------------------------------------------------------|
| 0x04    | Temperature            | 2 bytes, 16-bit signed integer (LE), 0.1 °C             |
| 0x06    | Humidity               | 2 bytes, 16-bit signed integer (LE), 0.1 %              |
| 0x0A    | Battery                | 1 byte, 8-bit unsigned integer, 1 %                     |
| 0x0D    | Temperature + Humidity | 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 % |

### 3.2.2. ESP32 BLE Arduino

Pour connaitre l'addresse MAC du capteur Xiaomi nous avons utilisé l'application *nRF Connect*. Avoir son addresse nous permettra par la suite de filtrer les paquets que nous recevons afin de ne garder que ceux du capteur.

* Pour mettre en place le scan du ble, il faut d'abord importer les librairies suivantes :
    ```
    #include <BLEDevice.h>
    #include <BLEUtils.h>
    #include <BLEScan.h>
    #include <BLEAdvertisedDevice.h>
    ```

* Puis, je créer une classe qui me permettra de stocker les résultats et je déclare mes variables globales :
    ```
    class BLEResult
    {
        public:
        double temperature = -200.0f;
        double humidity = -1.0f;
        int16_t battery_level = -1;
    };

    BLEResult result;
    int scanTime = 1.5; //In seconds
    BLEScan *pBLEScan;
    ```

* Je créer ma fonction de *callback* qui sera appelée à chaque fois que l'on aura détecté un nouveau paquet d'advertising. Cette fonction va détecter la présence de data (type ID + 0x10 + taille + ...). Si elle en détecte, elle va alors mettre à jour mon objet ```BLEResult result``` avec les nouvelles valeurs.
    ```
    class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
    {
        void onResult(BLEAdvertisedDevice advertisedDevice)
        {
            BLEAddress address = advertisedDevice.getAddress();

            // Filter by mac address of mi temp
            if (address.toString() == "58:2d:34:3b:7d:3c")
            {

                uint8_t *payloadRaw = advertisedDevice.getPayload();
                size_t payloadLength = advertisedDevice.getPayloadLength();

                // For each byte of ble advertise
                for (int i = 0; i < payloadLength; i++)
                {

                    // Need min 3 char to start to check
                    if (i > 3)
                    {
                        uint8_t raw = payloadRaw[i - 3];     // type
                        uint8_t check = payloadRaw[i - 2];   // must always be 0x10
                        int data_length = payloadRaw[i - 1]; // length of data

                        if (check == 0x10)
                        {
                            // temperature, 2 bytes, 16-bit signed integer (LE), 0.1 °C
                            if ((raw == 0x04) && (data_length == 2) && (i + data_length <= payloadLength))
                            {
                                const int16_t temperature = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
                                result.temperature = temperature / 10.0f;
                            }
                            // humidity, 2 bytes, 16-bit signed integer (LE), 0.1 %
                            else if ((raw == 0x06) && (data_length == 2) && (i + data_length <= payloadLength))
                            {
                                const int16_t humidity = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
                                result.humidity = humidity / 10.0f;
                            }
                            // battery, 1 byte, 8-bit unsigned integer, 1 %
                            else if ((raw == 0x0A) && (data_length == 1) && (i + data_length <= payloadLength))
                            {
                                result.battery_level = payloadRaw[i + 0];
                            }
                            // temperature + humidity, 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 %
                            else if ((raw == 0x0D) && (data_length == 4) && (i + data_length <= payloadLength))
                            {
                                const int16_t temperature = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
                                const int16_t humidity = uint16_t(payloadRaw[i + 2]) | (uint16_t(payloadRaw[i + 3]) << 8);
                                result.temperature = temperature / 10.0f;
                                result.humidity = humidity / 10.0f;
                            }
                        }
                    }
                }
                displayResult();
            }
        }
    };
    ```

* Après, dans la fonction *setup* j'initialise le ble scan :
    ```
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan(); //create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); // less or equal setInterval value
    ```
    Vous pouvez voir que l'on défini une fonction de *callback* qui est celle définie précedement.

* Enfin, dans la fonction *loop* je scan continuellement les paquets d'avertising :
    ```
    BLEScanResults foundDevices = pBLEScan->start(scanTime, false);

    pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
    ```

## 3.3. PubSubClient MQTT - Wifi

* Pour mettre en place le wifi et le MQTT, il faut d'abord importer les librairies suivantes :
    ```
    #include <WiFi.h>
    #include <PubSubClient.h>
    #include <Wire.h>
    #include <SPI.h>
    ```

* Puis, je reéfinis mes paramètres et je créer des variables globales stockant les différents paramètres du wifi et MQTT :
    ```
    #define BME_SCK 18
    #define BME_MISO 19
    #define BME_MOSI 23
    #define BME_CS 5

    const char *ssid = "B127-EIC";
    const char *password = "b127-eic";
    //const char *ssid = "Luciano";
    //const char *password = "123456789";
    const char *mqtt_server = "192.168.1.105";
    const char *mqtt_topic_temp = "topic/temp";
    const char *mqtt_topic_hum = "topic/hum";
    const char *mqtt_topic_bat = "topic/bat";
    const int mqtt_port = 1883;

    WiFiClient espClient;
    PubSubClient client(espClient);
    ```
    Nous avons utilisé le site suivant http://hivemq.com/demos/websocket-client/ pour tester que notre MQTT fonctionne correctement. En effet, il suffisait juste de mettre ```mqtt_server = "broker.mqttdashboard.com"``` et ```mqtt_port = 1883``` afin de configurer l'adresse du site. Puis, sur le site, il fallait subscribe aux topics ```topic/temp```, ```topic/hum```, ```topic/bat``` pour voir les valeurs que la carte ESP32 publish.

* J'implémente ma fonctions pour me connecter au wifi. Celle-ci va essayer de se connecter uniquement si l'on n'est pas connecté. En outre, si l'on n'est pas connecté, on essaye de se connecter toutes les 500ms jusqu'à y arriver.
    ```
    void setup_wifi()
    {
        // We start by connecting to a WiFi network

        if (WiFi.status() != WL_CONNECTED)
        {
            WiFi.begin(ssid, password);

            while (WiFi.status() != WL_CONNECTED)
            {
              delay(500);
              displayResult();
            }
            
        }

        displayResult();
    }
    ```

* J'implémente ma fonction de callback qui sera appelée lorsque je recevrai un résultat si je subscribe à un topic avec MQTT. Or, nous allons juste publish sur différents topic donc la fonction est vide.
    ```
    void callback(char *topic, byte *message, unsigned int length)
    {
    }
    ```

* Puis, je créer ma fonction pour me reconnecter au serveur MQTT. Celle-ci va essayer de se connecter uniquement si l'on n'est pas connecté. De plus, si l'on n'est pas connecté, on essaye de se connecter toutes les 5s jusqu'à y arriver.
On créer un ID unique pour la carte ESP32 lors de la connection.
    ```
    void reconnect()
    {
        Serial.print("reconnect...");

        // Loop until we're reconnected
        while (!client.connected())
        {
            // Create a random client ID
            String clientId = "ESP32Client-";
            clientId += String(random(0xffff), HEX);

            // Attempt to connect
            if (client.connect(clientId.c_str()))
            {
                Serial.println("ok");
            }
            else
            {
                // Wait 5 seconds before retrying
                delay(5000);
            }
        }
    }
    ```

* Après, dans la fonction *setup* j'initialise le wifi et la connection au serveur MQTT :
    ```
    // Initialise wifi
    setup_wifi();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    ```
    Vous pouvez voir que l'on défini une fonction de *callback* qui est celle définie précedement.

* Enfin, dans la fonction *loop* je me reconnecte au wifi et MQTT si besoin puis je publish la temperature et/ou l'humidité et/ou le niveau de batterie du capteur Xiaomi si les variables existent et que l'on est bien connecté au wifi :
    ```
    setup_wifi();

    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    if (result.temperature > -200.0f && WiFi.status() == WL_CONNECTED)
    {
        char c[10];
        sprintf(c, "%.2f", result.temperature);
        client.publish(mqtt_topic_temp, c);
        Serial.println("Sent temperature");
    }
    if (result.humidity > -1.0f && WiFi.status() == WL_CONNECTED)
    {
        char c[10];
        sprintf(c, "%.2f", result.humidity);
        client.publish(mqtt_topic_hum, c);
        Serial.println("Sent humidity");
    }
    if (result.battery_level > -1 && WiFi.status() == WL_CONNECTED)
    {
        char c[10];
        sprintf(c, "%d", result.battery_level);
        client.publish(mqtt_topic_bat, c);
        Serial.println("Sent battery_level");
    }
    ```

## 3.4. TFT_eSPI Ecran
* Afin de configurer les dimensions de l'écran, nous avons modifié le fichier suivant ```.pio/libdeps/ttgo-lora32-v1/TFT_eSPI/TFT_Drivers/ILI9341_Defines.h```. Avant cette modification, l'affichage ne prenait pas tout l'écran.
    ```
    #define TFT_WIDTH  320
    #define TFT_HEIGHT 480
    ```

* Pour mettre en place l'affichage sur l'écran, il faut d'abord importer la librairie suivante :
    ```
    #include <TFT_eSPI.h>
    ```

* Puis je définis ma fonction d'affichage d'écran et les variables globales pour modifier la couleur du texte lorsque l'on appuie sur le bouton gauche :
    ```
    void displayResult();

    // Buttons
    #define BUTTONGAUCHE 0  // btn de gauche

    int cptBtnG = 0; // compteur appui btn de gauche

    uint32_t textColor[5] = {0x001F, 0xAFE5, 0x07FF, 0x7BE0, 0xFFFF};

    TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h
    ```

* J'implémente ma fonction d'affichage sur l'écran. Celle-ci permet d'afficher l'état de la connection au wifi ainsi que la température et/ou l'humidité et/ou l'état de la batterie du capteur Xiaomi si les variables existent :
    ```
    void displayResult()
    {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(100, 60);
        tft.println("HOME");

        if (result.temperature > -200.0f)
        {
            tft.setCursor(80, 90);
            tft.println("T:");
            tft.setCursor(110, 90);
            tft.println(result.temperature);
        }
        if (result.humidity > -1.0f)
        {
            tft.setCursor(80, 120);
            tft.println("H:");
            tft.setCursor(110, 120);
            tft.println(result.humidity);
        }
        if (result.battery_level > -1)
        {
            tft.setCursor(80, 150);
            tft.println("Power:");
            tft.setCursor(150, 150);
            tft.println(result.battery_level);
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            tft.setCursor(65, 200);
            tft.println("Connected");
        }
        else
        {
            tft.setCursor(110, 200);
            tft.println("No");
            tft.setCursor(65, 220);
            tft.println("Connected");
        }
    }
    ```

* Puis, j'implémente la fonction permettant d'initialiser l'écran en définissant la taille du texte et en affichant *Hello* :
    ```
    void initDevice()
    {
        tft.begin();
        tft.setRotation(1);
        tft.setTextSize(2);
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(80, 80);
        tft.println("Hello");
        tft.setCursor(80, 120);
        tft.println("DomoTic");
        tft.setCursor(80, 170);
        tft.println("Ver1.00");
    }
    ```

* Ensuite, j'implémente la fonction de callback de modification de la couleur du texte lorsquel'on appuie sur le bouton gauche :
    ```
    void gestionAppuiBtnGauche()
    {
        cptBtnG = cptBtnG + 1;
        if (cptBtnG > 4)
        {
            cptBtnG = 0;
        }
        tft.setTextColor(textColor[cptBtnG]);
    }
    ```
    Il y a 5 couleurs possibles.

* Après, dans la fonction *setup* j'initialise l'écran ainsi que l'évènement lors de l'appuie sur le bouton gauche :
    ```
    // Initialise screen
    initDevice();

    // gestion appuie btn
    attachInterrupt(BUTTONGAUCHE, gestionAppuiBtnGauche, FALLING);
    ```
    Vous pouvez voir que l'on défini une fonction de *callback* pour ```attachInterrupt``` qui est celle définie précedement.

* Enfin, dans la fonction *displayResult* est appelée dans différentes fonctions pour mettre à jour l'affichage :
    - *setup_wifi* si l'état de la connection change
    - *onResult* de la class *MyAdvertisedDeviceCallbacks* pour afficher les valeurs reçu par bluetooth

# 4. STM32
## 4.1. MQTT
### 4.1.1. Mise en place
* Pour la réalisation du projet pour la carte STM32 il faut se baser sur l'exemple Ethernet disponible dans l'ide. Par la suite, nous avons rajouter un fichier "mqtt.c" contenant les fonctions dont nous aurons besoin.  

* L'exemple ethernet permet d'initialiser les connexions de bases. Nous avons opter de faire la routine mqtt dans le thread "Start_Thread". 
  - **la première étape consiste à initialiser un client mqtt** : 
  ```
  mqtt_client_t *client = mqtt_client_new();
  ```
  - et à appeler la fonction ```example_do_connect(client)``` :
```  
   void example_do_connect(mqtt_client_t *client)
{
  struct mqtt_connect_client_info_t ci;
  err_t err;
  /* Setup an empty client info structure */
  memset(&ci, 0, sizeof(ci));

  ci.client_id = "lwip_test";
  ci.keep_alive = 600;
  int PORT = 1884;                          // port de connection au brooker
  ip_addr_t mqttServerIP;  
  IP4_ADDR(&mqttServerIP, 192, 168, 1, 100); // addr. IP du brooker 

// Connection au brooker MQTT
  err = mqtt_client_connect(client, &mqttServerIP, PORT, mqtt_connection_cb, 0, &ci);
  if(err != ERR_OK) {
    printf("Error : mqtt_connect return %d\n", err);
  }else{}
}
```

### 4.1.2. Connection au brooker
**La deuxième étape consiste à se connecter au brooker MQTT.**  Elle se fait avec la fonction ```mqtt_client_connect(mqtt_client_t *client, const ip_addr_t *ip_addr, u16_t port, mqtt_connection_cb_t cb, void *arg,
                    const struct mqtt_connect_client_info_t *client_info)```
Cette fonction prend en paramètre :
- un client MQTT
- l'adresse IP du brooker
- le port de connection
- une fonction de callback
- un paramètre à passer à la fonction de callback
- des informations sur le client

La fonction de callback, ```mqtt_connection_cb```, permet d'effectuer une routine si la connection réussie ou échoue. 
```
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
  err_t err, err1, err2;
  if(status == MQTT_CONNECT_ACCEPTED) {
    printf("mqtt_connection_cb: Connection reussie\n");

    /* Setup callback for incoming publish requests */
    mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);
    /* Subscribe to a topic named "subtopic" with QoS level 1, call mqtt_sub_request_cb with result */
    err = mqtt_subscribe(client, "topic/temp", 0, mqtt_sub_request_cb, "temp");
    err1 = mqtt_subscribe(client, "topic/hum", 0, mqtt_sub_request_cb, "hum");
    err2 = mqtt_subscribe(client, "topic/bat", 0, mqtt_sub_request_cb, "batt");
    printf("%d",&err);
    if(err != ERR_OK)  {printf("Erreur abonnement au Topic Temperature");}
    if(err1 != ERR_OK) {printf("Erreur abonnement au Topic Humidite");}
    if(err2 != ERR_OK) {printf("Erreur abonnement au Topic Batterie");}
  } else {
    printf("mqtt_connection_cb: Disconnected, reason: %d\n", status);

    /* Its more nice to be connected, so try to reconnect */
    example_do_connect(client);
  }
}
```
Si la connection au brooker réussie, nous affichons un message et appelons la fonction qui permet de s'abonner à un topic. 
### 4.1.3. Abonnnement à un topic
**La troisième étape consiste à s'abonner à un topic.** Nous utilisons pour cela la fonction ```mqtt_subscribe``` qui prend en argument  le client MQTT, le nom du topic, le niveau de QoS, une fonction de callback ainsi qu'un paramètre qui lui sera fournit.
```
mqtt_subscribe(client, topic, QoS, mqtt_sub_request_cb, arg);
```

* La fonction de callback ```mqtt_sub_request_cb``` est appelée après la tentative d'abonnement. Elle permet de programmer des routines à exécuter en fonction du résultat de la tentative. Dans notre cas, nous affichons un message :
```
static void mqtt_sub_request_cb(void *arg, err_t result)
{
  if (result == ERR_OK){
      printf("Abonnement reussi au Topic : %s\n", arg);
  }else{
      printf("Abonnement echoue au Topic : %s\n", arg);
      }
}
```

### 4.1.4. Recevoir des messages du brooker
**La quatrième étape consiste à définir les routines à exécuter lors de la publication de messages sur nos topics.** 
* La fonction ```mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);``` permet de définir les fonctions de callback quand nous recevons un publish de la part du brooker. 

Elle prend en argument : 
- le client MQTT
- une fonction callback pour gérer les publications entrantes en fonction de leur topic
- une fonction callback qui permet de gérer le contenu des publications
```
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
//  printf("Incoming publish at topic %s with total length %u\n", topic, (unsigned int)tot_len);
  /* Decode topic string into a user defined reference */
  if(strcmp(topic, "topic/temp") == 0) {
    inpub_id = 0;}
  if(strcmp(topic, "topic/hum") == 0) {
      inpub_id = 1;}
  if(strcmp(topic, "topic/bat") == 0) {
      inpub_id = 2;}
}
```
Dans la callback ``` mqtt_incoming_publish_cb ``` nous définissons un id ``` inpub_id ``` en fonction du topic de la publication entrante. Cet id sera utilisé dans la fonction de callback ``` mqtt_incoming_data_cb ```.
```
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
//  printf("Incoming publish payload with length %d, flags %u\n", len, (unsigned int)flags);
  if(flags & MQTT_DATA_FLAG_LAST) {
    /* Last fragment of payload received (or whole part if payload fits receive buffer
       See MQTT_VAR_HEADER_BUFFER_LEN)  */

    /* Call function or do action depending on reference, in this case inpub_id */
    if(inpub_id == 0) {
      /* Don't trust the publisher, check zero termination */
    	if (len != 0 ){printf("Temp : ");
    	for (int i=0; i<len;i++){
    		printf("%c", (const char)data[i]);
    	}
    	printf("\n");}
    } else if(inpub_id == 1) {
    	if (len != 0 ){
            printf("Humidite : ");
    	    	for (int i=0; i<len;i++){
    	    		printf("%c", (const char)data[i]);
    	    	}
    	    	printf("\n");}
    }
    else if(inpub_id == 2) {
    	if (len != 0 ){
            printf("Batterie : ");
    	    for (int i=0; i<len;i++){
    	    	printf("%c", (const char)data[i]);
    	    }
    	    printf("\n");
        }
        } 
        else {
            printf("Topic inconnu \n");
                }
  } else {
    /* Handle fragmented payload, store in buffer, write to file or whatever */
  }
}

```
Dans cette fonction, en fonction du topic, donc ```input_id```, nous traitons la donnée, ```data```, et l'affichons sur l'écran de la carte.





## 4.2. ServoMoteur
Pour la partie servomoteur nous sommes partie d'un projet vide et nous avons configuré les pins pour diriger le servomoteur.
Pour configurer le pin, nous l'avons mis en TIMER PWM et l'IDE a généré automatiquement le code de configuration.
Nous n'avons pas eu le temps d'incorporer la partie servomoteur à la partie MQTT.

Voici la fonction permettant de bouger le moteur, d'abord on initialise le pin en timer.
Puis, je configure le timer.
Enfin, je lance le timer à la position souhaitée (en fonction de la valeur de l'humidité).S
```
void change_motor(int value)
{
  HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
  TIM_OC_InitTypeDef sConfig;
  sConfig.OCMode = TIM_OCMODE_PWM1;
  sConfig.Pulse = value;
  sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfig.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfig.OCFastMode = TIM_OCFAST_DISABLE;
  sConfig.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfig.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfig, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}
```

# 5. Annexe
## 5.1. MQTT Server Windows
* Connaitre l'adresse IP de l'ordinateur windows qui sera le MQTT server :
  ```
  ipconfig
  ```
* Modifier le fichier ```C:\Program Files\Mosquitto\mosquitto.conf``` :
  ```
  listener 1883 192.168.1.105 # Adresse IP de l'ordinateur windows qui sera le MQTT server
  allow_anonymous true
  ```
* Lancer le serveur :
  - Dans la barre de recherche, taper *services* puis *entrer*
  - Trouver *Mosquitto Broker*, click droit puis *Démarrer* ou *Arrêter*
* Verifier que le serveur MQTT est lancé :
  ```
  netstat
  ```
  Il faut avoir une ligne de ce type ```TCP    192.168.1.105:1883     192.168.1.103:59368    ESTABLISHED```.
* http://www.steves-internet-guide.com/install-mosquitto-windows-xp/
* https://mosquitto.org/download/

## 5.2. ESP32 TTGO - T-display
```
#include <Arduino.h>

// For the screen
#include <TFT_eSPI.h>

// For bluetooth
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// For wifi and mqtt
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>

#define BME_SCK 18
#define BME_MISO 19
#define BME_MOSI 23
#define BME_CS 5

void displayResult();

// Buttons
#define BUTTONGAUCHE 0  // btn de gauche

// Global var
//const char *ssid = "B127-EIC";
//const char *password = "b127-eic";
const char *ssid = "Luciano";
const char *password = "123456789";
const char *mqtt_server = "192.168.1.105";
const char *mqtt_topic_temp = "topic/temp";
const char *mqtt_topic_hum = "topic/hum";
const char *mqtt_topic_bat = "topic/bat";
const int mqtt_port = 1883;

int cptBtnG = 0; // compteur appui btn de gauche

uint32_t textColor[5] = {0x001F, 0xAFE5, 0x07FF, 0x7BE0, 0xFFFF};

int scanTime = 1.5; //In seconds
BLEScan *pBLEScan;

TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

// Result of ble mi temp
class BLEResult
{
public:
  double temperature = -200.0f;
  double humidity = -1.0f;
  int16_t battery_level = -1;
};

BLEResult result;

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi()
{
  // We start by connecting to a WiFi network

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {

      delay(500);
      displayResult();
      //Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  displayResult();
}

void callback(char *topic, byte *message, unsigned int length)
{
}

void reconnect()
{
  Serial.print("reconnect...");

  // Loop until we're reconnected
  while (!client.connected())
  {
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    // Attempt to connect
    if (client.connect(clientId.c_str()))
    {
      Serial.println("ok");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void displayResult()
{
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(100, 60);
  tft.println("HOME");

  if (result.temperature > -200.0f)
  {
    Serial.printf("temperature: %.2f", result.temperature);
    Serial.println();

    tft.setCursor(80, 90);
    tft.println("T:");
    tft.setCursor(110, 90);
    tft.println(result.temperature);
  }
  if (result.humidity > -1.0f)
  {
    Serial.printf("humidity: %.2f", result.humidity);
    Serial.println();

    tft.setCursor(80, 120);
    tft.println("H:");
    tft.setCursor(110, 120);
    tft.println(result.humidity);
  }
  if (result.battery_level > -1)
  {
    Serial.printf("battery_level: %d", result.battery_level);
    Serial.println();

    tft.setCursor(80, 150);
    tft.println("Power:");
    tft.setCursor(150, 150);
    tft.println(result.battery_level);
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    tft.setCursor(65, 200);
    tft.println("Connected");
  }
  else
  {
    tft.setCursor(110, 200);
    tft.println("No");
    tft.setCursor(65, 220);
    tft.println("Connected");
  }
}

// Callback when find device ble
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    BLEAddress address = advertisedDevice.getAddress();

    // Filter by mac address of mi temp
    if (address.toString() == "58:2d:34:3b:7d:3c")
    {

      uint8_t *payloadRaw = advertisedDevice.getPayload();
      size_t payloadLength = advertisedDevice.getPayloadLength();

      Serial.println();
      Serial.println("################################");
      Serial.print("Raw: ");

      // For each byte of ble advertise
      for (int i = 0; i < payloadLength; i++)
      {
        // Show the byte
        Serial.printf("%02X ", payloadRaw[i]);

        // Need min 3 char to start to check
        if (i > 3)
        {
          uint8_t raw = payloadRaw[i - 3];     // type
          uint8_t check = payloadRaw[i - 2];   // must always be 0x10
          int data_length = payloadRaw[i - 1]; // length of data

          if (check == 0x10)
          {
            // temperature, 2 bytes, 16-bit signed integer (LE), 0.1 °C
            if ((raw == 0x04) && (data_length == 2) && (i + data_length <= payloadLength))
            {
              const int16_t temperature = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
              result.temperature = temperature / 10.0f;
            }
            // humidity, 2 bytes, 16-bit signed integer (LE), 0.1 %
            else if ((raw == 0x06) && (data_length == 2) && (i + data_length <= payloadLength))
            {
              const int16_t humidity = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
              result.humidity = humidity / 10.0f;
            }
            // battery, 1 byte, 8-bit unsigned integer, 1 %
            else if ((raw == 0x0A) && (data_length == 1) && (i + data_length <= payloadLength))
            {
              result.battery_level = payloadRaw[i + 0];
            }
            // temperature + humidity, 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 %
            else if ((raw == 0x0D) && (data_length == 4) && (i + data_length <= payloadLength))
            {
              const int16_t temperature = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
              const int16_t humidity = uint16_t(payloadRaw[i + 2]) | (uint16_t(payloadRaw[i + 3]) << 8);
              result.temperature = temperature / 10.0f;
              result.humidity = humidity / 10.0f;
            }
          }
        }
      }

      Serial.println();
      displayResult();

      Serial.println("################################");
    }
  }
};

void initDevice()
{
  tft.begin();
  tft.setRotation(1);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(80, 80);
  tft.println("Hello");
  tft.setCursor(80, 120);
  tft.println("DomoTic");
  tft.setCursor(80, 170);
  tft.println("Ver1.00");
}
void gestionAppuiBtnGauche()
{
  cptBtnG = cptBtnG + 1;
  if (cptBtnG > 4)
  {
    cptBtnG = 0;
  }
  tft.setTextColor(textColor[cptBtnG]);
}

void setup()
{
  // Monitor speed
  Serial.begin(115200);

  // Initialise screen
  initDevice();

  // Initialise wifi
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Initialise BLE scan
  Serial.println("Scanning...");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value

  // gestion appuie btn
  attachInterrupt(BUTTONGAUCHE, gestionAppuiBtnGauche, FALLING);
}

void loop()
{
  setup_wifi();

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  if (result.temperature > -200.0f && WiFi.status() == WL_CONNECTED)
  {
    char c[10];
    sprintf(c, "%.2f", result.temperature);
    client.publish(mqtt_topic_temp, c);
    Serial.println("Sent temperature");
  }
  if (result.humidity > -1.0f && WiFi.status() == WL_CONNECTED)
  {
    char c[10];
    sprintf(c, "%.2f", result.humidity);
    client.publish(mqtt_topic_hum, c);
    Serial.println("Sent humidity");
  }
  if (result.battery_level > -1 && WiFi.status() == WL_CONNECTED)
  {
    char c[10];
    sprintf(c, "%d", result.battery_level);
    client.publish(mqtt_topic_bat, c);
    Serial.println("Sent battery_level");
  }

  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);

  pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
}
```
## 5.3. STM32 MQTT - Publier des messages 
Dans notre projet nous n'avions pas à publier des messages depuis la carte STM32.
Nous ajoutons quand même ci-dessous le code pour publier un message sur un topic depuis la carte STM32, pour ceux et celles qui voudraient le faire.

```
void example_publish(mqtt_client_t *client, void *arg)
{
  const char *pub_payload= "Ce message est à publier";*
  const char *topic="monTopic"
  err_t err;
  u8_t qos = 0; /* 0 1 or 2, see MQTT specification */
  u8_t retain = 0; /* No don't retain such crappy payload... */
  err = mqtt_publish(client, topic, pub_payload, strlen(pub_payload), qos, retain, mqtt_pub_request_cb, arg);
  if(err != ERR_OK) {
    printf("Publish err: %d\n", err);
  }
}

/* Called when publish is complete either with sucess or failure */
static void mqtt_pub_request_cb(void *arg, err_t result)
{
  if(result != ERR_OK) {
    printf("Publish result: %d\n", result);
  }
}


```
