/***************************** ACCESOCENTRORFID *******************************
 Descripción: Lectura de TAG ID por RFID y envío formateado por ETHERNET a servidor
 HTTP. Se espera respuesta que autoriza (LED verde) o no el acceso (LED rojo)

 Versión: 1.0
 Autor: Rubén Barrilero Regadera
 Fecha: 24/06/2019
*******************************************************************************/

/***************************** LIBRERIAS **************************************/
#include <SoftwareSerial.h>
#include <SPI.h>
#include <Ethernet.h>
#include <avr/wdt.h>

/***************************** CONSTANTES **************************************/
//Definición de Pines
#define RFIDTX 8
#define RFIDRX 9
#define LED_RED_EXT_DOOR 4
#define LED_GREEN_EXT_DOOR 5
#define TIME_DOOR_OPEN 2000 //2 segundos activo cierre electromagnético
#define TIME_DELETE_CURRENT_TAG 5000
#define BUFFER_CURRENT_TAGS 20
const int RFID_FRAME_SIZE = 14; // RFID DATA FRAME FORMAT: 1byte head (value: 2), 10byte data (2byte version + 8byte tag), 2byte checksum, 1byte tail (value: 3) https://www.youtube.com/watch?v=l8RDbHd1cak
const int RFID_TAG_ID_SIZE = 10; // 10byte data (2byte version + 8byte tag)
const byte mac[] = { 0x90, 0xA2, 0xDA, 0x0F, 0x5C, 0xA2 }; //Cambiar según dispositivo

/***************************** VARIABLES GLOBALES **************************************/
SoftwareSerial rfidSerial1 = SoftwareSerial(RFIDTX,RFIDRX);
IPAddress ip(192, 168, 50, 24);
EthernetClient ethernetClient;
byte bRFID_FRAME[RFID_FRAME_SIZE]; // used to store an incoming data frame 
int iRFID_FRAME_index = 0;
unsigned long ulTimeOutRFIDRef, ulOpenTimeRef;
IPAddress server(192,168,50,25);
char cScriptServer[] = "/open.php?id=";
boolean bWaitingServerRP=false;
struct sTimeOutTAG {
  unsigned long ulTagID;
  unsigned long ulTimeFound;
};
byte bNextCurrentTAG=0;
sTimeOutTAG sTimeOutTag[BUFFER_CURRENT_TAGS] = {0};

/***************************** FUNCIONES PRINCIPALES **************************************/
void setup() 
{
  wdt_disable(); // Desactivar el watchdog mientras se configura
  pinMode(LED_RED_EXT_DOOR, OUTPUT);
  pinMode(LED_GREEN_EXT_DOOR, OUTPUT);
  Serial.begin(9600);
  rfidSerial1.begin(9600);
  Ethernet.begin(mac, ip); 
  delay(1000); //1seg para inicializar EthernetShield
  if (ethernetClient.connect(server, 80))
  {
    Serial.println("Connected to server");
  }
  else 
  {
    Serial.println("Connection to server cannot be established");
  }
  wdt_enable(WDTO_8S); //Configuramos el WD a 8 sengundos
}

void loop() 
{
  unsigned long ulTAG_ID=0;
  if (rfidSerial1.available() > 0)
  {
    int iCurrentByte = rfidSerial1.read(); //leemos un byte
    if (iCurrentByte == -1) //no hay dato válido
    { 
      iRFID_FRAME_index=0;
      rfidSerial1.flush();
    }
    else
    {
      if (iCurrentByte == 2) //Inicio de trama
      {
         iRFID_FRAME_index=0;
      }
      bRFID_FRAME[iRFID_FRAME_index++] = (byte)iCurrentByte;  
      if(iCurrentByte == 3) //Final de trama, comprobamos el ID
      {
        boolean bTAGwasPrevFound=false;
        ulTAG_ID=leerTAGID(&bRFID_FRAME[0]);
        refreshBufferFoundTAGs();
        for (int i=0; i<bNextCurrentTAG; i++)
        {
          if (ulTAG_ID==sTimeOutTag[i].ulTagID)
          {
            bTAGwasPrevFound=true;
          }
        }
        if (bTAGwasPrevFound==false)
        {
          Serial.println(ulTAG_ID); //bRuben
          ethernetClient.print("GET ");
          ethernetClient.print(cScriptServer);
          ethernetClient.println(ulTAG_ID);
          ethernetClient.print("Host: ");
          ethernetClient.println(server);
//          ethernetClient.println("Connection: close");
//          ethernetClient.println(); //¿es necesario?
          bWaitingServerRP=true;
          sTimeOutTag[bNextCurrentTAG].ulTagID=ulTAG_ID;
          sTimeOutTag[bNextCurrentTAG].ulTimeFound=millis();   
          bNextCurrentTAG++;       
        }
      }
    } 
  }
  if (ethernetClient.available() && bWaitingServerRP==true)
  {
    Serial.print("Incoming data: ");
    byte rp = ethernetClient.read();
    Serial.println(rp);
    if (rp == 49)
    {
      digitalWrite(LED_GREEN_EXT_DOOR, HIGH);
    }
    else
    {
      digitalWrite(LED_RED_EXT_DOOR, HIGH);
    }
    ulOpenTimeRef=millis();
    bWaitingServerRP=false;
    ethernetClient.stop();
    ethernetClient.flush();
  }  
  if (millis()-ulOpenTimeRef > TIME_DOOR_OPEN)
  {
    digitalWrite(LED_RED_EXT_DOOR, LOW);
    digitalWrite(LED_GREEN_EXT_DOOR, LOW);
  }
  if (!ethernetClient.connected())
  { 
    ethernetClient.connect(server, 80);
  }
  wdt_reset(); // Actualizar el watchdog para que no produzca un reinicio
}

/***************************** FUNCIONES AUXILIARES **************************************/
unsigned long leerTAGID (byte *pRFID_Frame)
{
  byte bSendCS, bCalcCS;
  unsigned long ulTAG_ID = 0;

  bSendCS=ascii2Num(*(pRFID_Frame+11))*16+ascii2Num(*(pRFID_Frame+12));
  for (int i=1; i<= 10; i+=2)
  {
    byte bAux = ascii2Num(*(pRFID_Frame+i))*16+ascii2Num(*(pRFID_Frame+i+1)); 
    bCalcCS ^= bAux;
  }
  if (*pRFID_Frame==2 && bSendCS==bCalcCS && *(pRFID_Frame+RFID_FRAME_SIZE-1)==3) //Si Bytes de inicio y cierre Ok & Checksum OK
  {
    for (int i=3; i<=10; i++)
    {
      ulTAG_ID+=(unsigned long)ascii2Num(*(pRFID_Frame+i))*power((unsigned long)16,(unsigned long)(10-i));
    }
  }
  return ulTAG_ID;
}

unsigned long power (unsigned long a, unsigned long b)
{
  unsigned long resultado=a;
  if (b==0)
  {
    resultado=1;
  }
  for (int i=1; i< b; i++)
  {
    resultado *= a;
  }
  return resultado;
}

byte ascii2Num (byte value)
{
  byte bNum=0;
  if (value <= 0x39)
  { 
    bNum=value-0x30;
  }
  else
  {
    bNum=(value-0x41)+0xA;
  }
  return bNum;
}

void refreshBufferFoundTAGs(void)
{
  int i=bNextCurrentTAG;
  unsigned long ulCurrentTime=millis();
  while (i>0 && ulCurrentTime-sTimeOutTag[i-1].ulTimeFound < TIME_DELETE_CURRENT_TAG)
  {
    i--;
  }
  if (i != 0)
  {
    for (int j=i; j<bNextCurrentTAG; j++)
    {
      sTimeOutTag[j-i+1].ulTimeFound=sTimeOutTag[j].ulTimeFound;
      sTimeOutTag[j-i+1].ulTagID=sTimeOutTag[j].ulTagID;
    }
    bNextCurrentTAG=i-1;
  }
}



