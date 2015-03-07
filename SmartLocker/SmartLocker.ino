#include <Adafruit_CC3000.h>
#include <CC3000_MDNS.h>
#include <aREST.h>
#include <SPI.h>
#include <SD.h>
#include <SdFat.h>
#include "utility/debug.h"
#include "utility/socket.h"
#include "Timer.h"


// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!
// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10

#define thing_name "PSTArduino"

Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
                                         SPI_CLOCK_DIVIDER); // you can change this clock speed

#define WLAN_SSID       "ASUS"   // cannot be longer than 32 characters!
#define WLAN_PASS       "venturi53"
// Security can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
#define WLAN_SECURITY   WLAN_SEC_WPA2

#define LISTEN_PORT           80      // What TCP port to listen on for connections.  
                                      // The HTTP protocol uses port 80 by default.
                                      
                                                          
#define MAX_ACTION            10      // Maximum length of the HTTP action that can be parsed.

#define MAX_PATH              64      // Maximum length of the HTTP request path that can be parsed.
                                      // There isn't much memory available so keep this short!

#define BUFFER_SIZE           MAX_ACTION + MAX_PATH + 20  // Size of buffer for incoming request data.
                                                          // Since only the first line is parsed this
                                                          // needs to be as large as the maximum action
                                                          // and path plus a little for whitespace and
                                                          // HTTP version.

#define TIMEOUT_MS            500    // Amount of time in milliseconds to wait for
                                     // an incoming request to finish.  Don't set this
                                     // too high or your server could be slow to respond.
                                     

Adafruit_CC3000_Server httpServer(LISTEN_PORT);
uint8_t buffer[BUFFER_SIZE+1];
int bufindex = 0;
char action[MAX_ACTION+1];
char path[MAX_PATH+1];
File file;
String data = "";
int Lock_status = 0;
int open_closed;
int start_status = 0;
int running = 0;
int i = 0;
char c;
String test = "";

//Variable de l'adresse IP de dweet
uint32_t ip_dweet = 0;

//Variable booléenne d'authentification
boolean authentication = false;

//Tableau de passwords valides
String passwords[5] = {"1234", "yolo", "bite", "morray", "José"};

Timer t;


/***********************************************************************************************************/
/*                                                                                                         */
/*      Fontion setup                                                                                      */
/*       - Initialisation et vérification du système de fichiers sur la carte SD de l'Arduino MEGA         */
/*       - Vérification de l'existence de la page home.html sur la carte SD                                */ 
/*       - Initialisation de la connexion au réseau Internet spécifié préalablement au dessus              */
/*       - Affichage des détails de la connexion Internet (adresse IP, DNS, Gateway...)                    */
/*       - Lancement de la connexion à freeboard.io pour gestion du dashboard                              */
/*                                                                                                         */
/***********************************************************************************************************/

void setup(void)
{
  Serial.begin(115200);
  Serial.println(F("Lancement du systeme !"));
  
  // Initialisation du module CC3000
  Serial.println(F("\nDemaragge du CC3000..."));
  if (!cc3000.begin())
  {
    Serial.println(F("Couldn't begin()! Check your wiring?"));
    while(1);
  }
  
  pinMode(53, OUTPUT);
  
  //Initalisation de la carte SD
  if (!SD.begin(4)) {
    Serial.println("Erreur a l'initialisation !");
    return;
  }
  Serial.println("Initialisation des fichiers de la carte SD : Ok."); //Si l'initilisation réussie
  
  if (!SD.exists("Accueil.htm") || !SD.exists("Unlock.htm") || !SD.exists("Carnet.htm")) {  
     Serial.println("ERREUR - Un fichier html est absent !");  
  }
  
  Serial.print(F("\nConnexion en cours au reseau ")); 
  Serial.println(WLAN_SSID);
  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    Serial.println(F("Failed!"));
    while(1);
  }
  
  Serial.println(F("Connecté !"));
  
  Serial.println(F("Demande de DHCP"));
  while (!cc3000.checkDHCP())
  {
    delay(100); // ToDo: Insert a DHCP timeout!
  }
  
  // Display the IP address DNS, Gateway, etc.
  while (! displayConnectionDetails()) {
    delay(1000);
  }
  
  //Démarrage du serveur
  httpServer.begin();
  pinMode(2, OUTPUT);
  
  Serial.println(F("En attente de connexion..."));
  
  //Récupération de l'IP du dweet
  Serial.print(F("www.dweet.io -> "));
  while  (ip_dweet  ==  0)  {
    if  (!cc3000.getHostByName("www.dweet.io", &ip_dweet))  {
      Serial.println(F("Couldn't resolve!"));
    }
    delay(500);
  }  
  cc3000.printIPdotsRev(ip_dweet);
  Serial.println(F(""));

  //Chargement des données de démarrage
  running = 1;
  open_closed = start_status;
  update_state();
  
  //Update de l'état du système toutes les 10 minutes (600 000 ms)
  t.every(600000, update_state);

}





/***********************************************************************************************************/
/*                                                                                                         */
/*      Fontion loop                                                                                       */
/*      Lancement de l'attente des connexions sur l'Arduino pour renvoie de la page HTML                   */
/*      Lancement de la connexion à freeboard.io pour gestion du dashboard                                 */
/*                                                                                                         */
/***********************************************************************************************************/

void loop(void)
{ 
  
  //On update le dashboard à chaque tour de loop
  t.update();

  //On créer un client et on attend qu'il fasse une requête
  Adafruit_CC3000_ClientRef client = httpServer.available();
  if (client) {
    Serial.println(F("Client connected."));

    // On efface le buffer de la requête en attendant la prochaine
    bufindex = 0;
    memset(&buffer, 0, sizeof(buffer));

    // On efface le path et l'action pour la prochaine requête à lire
    memset(&action, 0, sizeof(action));
    memset(&path,   0, sizeof(path));
    
    //On effectue un timer pour donner le temps de recevoir la requête
    unsigned long endtime = millis() + TIMEOUT_MS;
    
    // Read all the incoming data until it can be parsed or the timeout expires.
    bool parsed = false;
    while (!parsed && (millis() < endtime) && (bufindex < BUFFER_SIZE)) {
      if (client.available()) {
        buffer[bufindex++] = client.read();
      }
      parsed = parseRequest(buffer, bufindex, action, path);
    }
  
    //Si la requête a bien été "parsé"
    if(parsed) {
      //On affiche quelques informations
      Serial.println(F("Processing request"));
      Serial.print(F("Action: ")); Serial.println(action);
      Serial.print(F("Path: ")); Serial.println(path);
      
      open_closed = ProcessSubmits(client);
      
      //Si la requête est faite sur la page Unlock, alors on reboucle sur cette même page
      if(ifContainsUnlock(path) == 1) {
        file = SD.open("Unlock.htm");
        if (file) {
          // lecture du fichier jusqu'à la fin:
          while (file.available()) {
            //On lit le fichier
            client.write(file.read());
          }
          // Fermeture du fichier:
          file.close();
        }else {
          // Ouverture impossible:
          Serial.print("Ouverture impossible de ");
          Serial.println("Unlock.htm");
        }
      }
      //Sinon, on affiche la page contenue dans le path de la requête utilisateur
      else {
        //On ouvre le fichier de la page HTML voulue
        file = SD.open(path);
        if (file) {
          // lecture du fichier jusqu'à la fin:
          while (file.available()) {
            //On lit le fichier
            client.write(file.read());
          }
          // Fermeture du fichier:
          file.close();
        }else {
          // Ouverture impossible:
          Serial.print("Ouverture impossible de ");
          Serial.println(path);
        }
      }
    }
  
  //On effectue un delay pour attendre que le client reçoive les données envoyées
  delay(100);
  
  //On affiche que la connexion va se fermer puis on ferme la connexion
  Serial.println(F("Client disconnected"));
  client.close();
  
  //On peut alors envoyer des données au Dashboard (s'il y en a)
  //Connexion au Dashboard pour envoyer les données de l'Arduino
  Adafruit_CC3000_Client client_dweet = cc3000.connectTCP(ip_dweet, 80);
  if (client_dweet.connected())  {
    Serial.print(F("Sending request... "));

      client_dweet.fastrprint(F("GET /dweet/for/"));
      client_dweet.print(thing_name);
      client_dweet.fastrprint(F("?open_closed="));
      client_dweet.print(open_closed);
      client_dweet.fastrprintln(F(" HTTP/1.1"));
      
      client_dweet.fastrprintln(F("Host: dweet.io"));
      client_dweet.fastrprintln(F("Connection: close"));
      client_dweet.fastrprintln(F(""));
      
      Serial.println(F("done."));
    } else {
      Serial.println(F("Connection failed"));    
      return;
    }
    
    Serial.println(F("Reading answer..."));
    while (client_dweet.connected()) {
      while (client_dweet.available()) {
        char c = client_dweet.read();
        Serial.print(c);
      }
    }
    Serial.println(F(""));
    
    //Fermeture de la connexion au Dashboard
    client_dweet.close();
   
  // Wait a short period to make sure the response had time to send before
  // the connection is closed (the CC3000 sends data asyncronously).
  delay(100);

  // Close the connection when done.
  
  }

}




/***********************************************************************************************************/
/*                                                                                                         */
/*      Fontions complémentaires                                                                           */
/*       - Fonction "bool displayConnectionDetails(void)" pour l'affichage des détails de connexion lors   */
/*         du setup de l'Arduino                                                                           */
/*       - Fonction "bool parseRequest(uint8_t* buf, int bufSize, char* action, char* path)" pour le       */
/*         parsage de la requête utilisateur sur les pages HTML contenues sur l'Arduino                    */
/*       - Fonction " void parseFirstLine(char* line, char* action, char* path)" pour le parsage de        */
/*         de l'action à réliser                                                                           */
/*       - Fonction "int ProcessSubmits(Adafruit_CC3000_ClientRef client)" pour le traitement du mot       */
/*         de passe saisi par l'utilisateur et action correspondante sur le PIN de sortie spécifié         */
/*                                                                                                         */
/***********************************************************************************************************/

// Fonction d'affichage des propriétés de connexion (adresse IP, netmask, adresse de la passerelle, adresse du serveur DHCP et adresse du serveur DNS)
bool displayConnectionDetails(void)
{
  uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;
  
  if(!cc3000.getIPAddress(&ipAddress, &netmask, &gateway, &dhcpserv, &dnsserv))
  {
    Serial.println(F("Unable to retrieve the IP Address!\r\n"));
    return false;
  }
  else
  {
    Serial.print(F("\nIP Addr: ")); cc3000.printIPdotsRev(ipAddress);
    Serial.print(F("\nNetmask: ")); cc3000.printIPdotsRev(netmask);
    Serial.print(F("\nGateway: ")); cc3000.printIPdotsRev(gateway);
    Serial.print(F("\nDHCPsrv: ")); cc3000.printIPdotsRev(dhcpserv);
    Serial.print(F("\nDNSserv: ")); cc3000.printIPdotsRev(dnsserv);
    Serial.println();
    return true;
  }
}


//Parsing de la requête utilisateur
bool parseRequest(uint8_t* buf, int bufSize, char* action, char* path) {
  // Check if the request ends with \r\n to signal end of first line.
  if (bufSize < 2)
    return false;
  if (buf[bufSize-2] == '\r' && buf[bufSize-1] == '\n') {
    parseFirstLine((char*)buf, action, path);
    return true;
  }
  return false;
}


// Parse the action and path from the first line of an HTTP request.
void parseFirstLine(char* line, char* action, char* path) {
  // Parse first word up to whitespace as action.
  char* lineaction = strtok(line, " ");
  if (lineaction != NULL)
    strncpy(action, lineaction, MAX_ACTION);
  // Parse second word up to whitespace as path.
  char* linepath = strtok(NULL, " ");
  if (linepath != NULL)
    strncpy(path, linepath, MAX_PATH);
}


//Fonction de vérification du password saisi et d'action sur le pin de la gâche
int ProcessSubmits(Adafruit_CC3000_ClientRef client)
{
  int j =0;
  char c;
  String data = "";
  
  //On rempli une STring avec le Path de la requête pour pouvoir comparer
  for(j=0;j<MAX_PATH+1;j++) {
    c = path[j];
    data +=c;
  }
  
  int k = 0;
  
  //On regarde si le bon password est contenu dans cette String
  for(k=0; k<5; k++) {
    if(data.indexOf(passwords[k]) > -1 ) {
      authentication = true;
      break;
    }
  }
  
  //Si la requête contient le bon mot de passe, alors on regarde quelle est l'action demandée
  if(authentication == true) {   
    //Si la requête HTTP contient ce paramètre, alors on change passe le flag à 1
    if (data.indexOf("Allumer=Ouvrir") > -1) {
      digitalWrite(2, HIGH);
      Lock_status = 1;    
    }
    else if(data.indexOf("Eteindre=Fermer") > -1) {
      digitalWrite(2, LOW);
      Lock_status = 0;
    }
  }
  
  //Affichage pour debugage : valeur de Lock_status
  Serial.print("Lock_status = ");Serial.println(Lock_status);
  //On retourne Lock_status pour s'en servir dans l'envoie des données au Dashboard via Dweet
  return Lock_status;
  
  //On remet le booléen "authentication" à false pour une prochaine requête
  authentication = false;
}

//Fonction d'update du statut du système (en fonctionnement ou non)
void update_state(){
    Adafruit_CC3000_Client client_first = cc3000.connectTCP(ip_dweet, 80);
    if (client_first.connected())  {
      Serial.print(F("Sending request... "));
      
            client_first.fastrprint(F("GET /dweet/for/"));
            client_first.print(thing_name);
            client_first.fastrprint(F("?running="));
            client_first.print(running);
            client_first.fastrprint(F("&open_closed="));
            client_first.print(open_closed);
            client_first.fastrprintln(F(" HTTP/1.1"));
            
            client_first.fastrprintln(F("Host: dweet.io"));
            client_first.fastrprintln(F("Connection: close"));
            client_first.fastrprintln(F(""));
            
            Serial.println(F("done."));
          } else {
            Serial.println(F("Connection failed"));    
            return;
          }
          
          Serial.println(F("Reading answer..."));
          while (client_first.connected()) {
            while (client_first.available()) {
              char c = client_first.read();
              Serial.print(c);
            }
          }
}

//Fonction qui test si la requête contient la page Unlock
int ifContainsUnlock(char* data) {
  
  int i = 0;
  char tmp;
  String j = "";
  
  for(i=0; i<MAX_PATH+1; i++) {
    tmp = data[i];
    j += tmp;
  }
  
  if(j.indexOf("Unlock") > -1) {
    Serial.println("The path contains Unlock !");
    return 1;
  }
  
  return 0;
  
}
