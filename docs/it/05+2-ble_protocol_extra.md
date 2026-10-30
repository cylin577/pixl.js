# PixLink (Sempre attivo)

Questo documento descrive **PixLink**, una piccola interfaccia BLE sempre attiva su Pixl.js.
È indipendente dal protocollo di trasferimento file basato sul Nordic UART Service (NUS)
documentato in `05+1-ble_protocol.md` e resta disponibile anche quando il dispositivo non è
in modalità trasferimento file.

Espone due comandi di primo livello, più un sotto-comando valido solo in modalità controllo:

1. Entra nella normale modalità di trasferimento file BLE.
2. Entra in modalità controllo, dove l'host invia 3 input che corrispondono ai 3 pulsanti fisici.
3. Aggiornamento dello stato dei pulsanti (`0x03`), un sotto-comando della modalità controllo.

## Servizio

- Nome del servizio: PixLink
- UUID del servizio: `7e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Caratteristica (write + write without response): `7e400002-b5a3-f393-e0a9-e50e24dcca9e` (Control TX)
- Caratteristica (notify): `7e400003-b5a3-f393-e0a9-e50e24dcca9e` (Control RX)

L'UUID del control plane viene annunciato nelle modalità Pixl.js e AmiLoop (entra nei 31 byte
dei dati di advertising). Nelle modalità amiibolink l'annuncio è già pieno, quindi il servizio
non viene annunciato in quel caso, ma esiste comunque sulla connessione. Non viene mai
disabilitato dall'ingresso in modalità trasferimento file o controllo: cambia solo la modalità
attiva.

Tutte le risposte vengono inviate come notifiche su Control RX (`7e400003`). L'host deve
abbonarsi a quella caratteristica (abilitare le notifiche) prima di inviare i comandi,
altrimenti il dispositivo non ha dove inviare la risposta e la scarta.

## Formato del pacchetto

Stessa intestazione di 4 byte del protocollo di trasferimento file:

| Nome del campo | Tipo | Lunghezza (byte) | Descrizione |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | tipo di comando |
| status | uint8 | 1 | 0 nelle richieste; codice di stato nelle risposte |
| chunk | uint16 | 2 | 0 (nessun chunking; i pacchetti di controllo sono una singola scrittura ATT) |
| data | byte | N | Dati relativi al comando specifico |

I tipi numerici sono interi senza segno, little endian. Non viene usato il trasferimento a
chunk: ogni comando entra in una singola scrittura ATT.

Le richieste dei comandi noti devono avere `status = 0` e `chunk = 0`. Una richiesta con un
campo status o chunk diverso da zero è malformata: `0x01` e `0x02` vengono rifiutati con lo
stato `0x01`, `0x03` (che non ha risposta) viene ignorato silenziosamente e un comando
sconosciuto viene sempre risposto con `0xFF`.

## Codici di stato

| Codice di stato | Descrizione |
| ---- | ---- |
| 0x00 | OK |
| 0x01 | Errore generico (richiesta malformata, oppure comando non accodato) |
| 0x02 | Transizione di modalità non valida (es. dispositivo già nella modalità richiesta) |
| 0xFF | Comando sconosciuto |

Un comando sconosciuto viene risposto con lo stesso byte di comando della richiesta e con lo
stato `0xFF`.

## Matrice delle modalità e dei comandi

| Modalità attuale | Comando | Azione | Risposta |
| ---- | ---- | ---- | ---- |
| inattivo | 0x01 | Avvia l'app di trasferimento file BLE, la modalità diventa trasferimento file | 0x00 |
| controllo | 0x01 | Termina e riavvia l'app di trasferimento file BLE (il launcher termina prima l'app in esecuzione, quindi l'interfaccia dell'app riparte), la modalità diventa trasferimento file | 0x00 |
| trasferimento file | 0x01 | Nessuna, il dispositivo è già in quella modalità | 0x02 |
| inattivo | 0x02 | La modalità diventa controllo | 0x00 |
| trasferimento file | 0x02 | La modalità diventa controllo | 0x00 |
| controllo | 0x02 | Nessuna, il dispositivo è già in quella modalità | 0x02 |
| controllo | 0x03 | Applica i 3 valori dei pulsanti, nessuna risposta | nessuna |
| qualsiasi altra modalità | 0x03 | Ignorato, nessuna risposta | nessuna |

Le risposte sono pacchetti di 4 byte: `{cmd, status, 0x00, 0x00}`, dove `cmd` è il byte di
comando della richiesta.

Il comando `0x01` dalla modalità inattiva o controllo passa dal launcher delle app: l'app in
esecuzione viene terminata prima. Dalla modalità trasferimento file il comando viene
rifiutato con `0x02` e non viene riavviato nulla.

## Comando 0x01: Entra nel normale trasferimento file BLE

Porta il dispositivo nella normale app di trasferimento file BLE (il protocollo basato su NUS
descritto in `05+1-ble_protocol.md`). Il servizio di trasferimento file NUS viene usato sulla
connessione già aperta: il dispositivo non annuncia un secondo servizio e non apre una seconda
connessione. L'advertising riprende quando la connessione viene chiusa. PixLink resta
disponibile per il cambio di modalità.

Richiesta:

| Nome del campo | Tipo | Lunghezza (byte) | Valore |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x01 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |

Risposta:

| Nome del campo | Tipo | Lunghezza (byte) | Valore |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x01 |
| status | uint8 | 1 | 0x00 se riuscita, 0x02 se già in modalità trasferimento file |
| chunk | uint16 | 2 | 0 |

## Comando 0x02: Entra in modalità controllo

Porta il dispositivo in modalità controllo. In questa modalità l'host comanda i 3 pulsanti
fisici via software. Un pulsante già premuto prima dell'inizio della modalità controllo
possiede ancora lo stato del pulsante e consegna il suo rilascio come di consueto, così
l'interfaccia non resta con un pulsante bloccato. Una pressione fisica che avviene durante
la modalità controllo viene ignorata del tutto (pressione, rilascio, pressione lunga e
ripetizione sono tutti soppressi) e non blocca l'input virtuale.

Se l'app di trasferimento file è in esecuzione continua a girare: cambia solo la modalità di
input.

Richiesta:

| Nome del campo | Tipo | Lunghezza (byte) | Valore |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x02 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |

Risposta:

| Nome del campo | Tipo | Lunghezza (byte) | Valore |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x02 |
| status | uint8 | 1 | 0x00 se riuscita, 0x02 se già in modalità controllo |
| chunk | uint16 | 2 | 0 |

### Input dei pulsanti (solo in modalità controllo)

Dopo essere entrato in modalità controllo, l'host invia gli aggiornamenti dello stato dei
pulsanti sulla stessa caratteristica Control TX usando il cmd `0x03` (un sotto-comando della
modalità controllo). Ogni aggiornamento porta 3 byte, uno per pulsante fisico, in ordine
fisso:

| Byte | Pulsante | Valore |
| ---- | ---- | ---- |
| 0 | Pulsante A (sinistro) | 0 = rilasciato, non zero = premuto |
| 1 | Pulsante B (centrale) | 0 = rilasciato, non zero = premuto |
| 2 | Pulsante C (destro) | 0 = rilasciato, non zero = premuto |

Pacchetto:

| Nome del campo | Tipo | Lunghezza (byte) | Descrizione |
| ---- | ---- | ---- | ---- |
| cmd | uint8 | 1 | 0x03 |
| status | uint8 | 1 | 0 |
| chunk | uint16 | 2 | 0 |
| btnA | uint8 | 1 | 0 = rilasciato, non zero = premuto |
| btnB | uint8 | 1 | 0 = rilasciato, non zero = premuto |
| btnC | uint8 | 1 | 0 = rilasciato, non zero = premuto |

Il pacchetto deve essere lungo esattamente 7 byte. Ogni byte di pulsante è un booleano:
0 = rilasciato, qualsiasi valore diverso da zero = premuto. Non c'è alcun debounce lato
software: solo un cambio di stato di un pulsante genera un evento. Un passaggio da 0 a 1
genera la pressione, un passaggio da 1 a 0 genera il rilascio, che conta anche come
pressione breve, esattamente come un pulsante fisico. Rinviare lo stesso valore non genera
alcun evento.

Se il pulsante è già premuto prima dell'inizio della modalità controllo, la pressione
fisica possiede ancora lo stato del pulsante: la pressione virtuale non genera un secondo
evento di pressione e il rilascio virtuale corrispondente non genera né rilascio né
pressione breve. Una pressione fisica avvenuta durante la modalità controllo viene ignorata
del tutto, quindi non possiede lo stato del pulsante e non blocca la pressione virtuale.

Per gli aggiornamenti `0x03` non viene inviata nessuna risposta (fire and forget), per
mantenere bassa la latenza. L'invio di `0x03` fuori dalla modalità controllo, o con un
pacchetto malformato, viene ignorato senza alcuna risposta.

## Uscita dalle modalità

- Per uscire dalla modalità controllo, invia il comando `0x01` (entra nel trasferimento file):
  il dispositivo lascia la modalità controllo ed entra nella modalità trasferimento file. Non
  esiste un comando separato "esci dalla modalità controllo"; le transizioni di modalità sono
  sempre avviate da uno dei due comandi di primo livello. Un ciclo di alimentazione riporta
  anche la modalità a inattivo.
- Terminare l'app di trasferimento file (per esempio quando l'utente la chiude) riporta la
  modalità PixLink a inattivo.
- Uscendo dalla modalità controllo i pulsanti virtuali ancora premuti vengono rilasciati
  (solo rilascio, nessuna pressione breve). Un pulsante virtuale la cui pressione non è
  stata consegnata all'interfaccia, perché la pressione fisica ne possedeva già lo stato,
  non viene rilasciato.
- Fuori dalla modalità controllo il comportamento dei pulsanti fisici non cambia, pressione
  lunga e ripetizione incluse. Mentre la modalità controllo è attiva tutti gli eventi dei
  pulsanti fisici sono soppressi, tranne il rilascio di un pulsante già premuto prima
  dell'inizio della modalità controllo.

## Note

- PixLink è sempre attivo; lo stato della modalità è tenuto in RAM e viene azzerato al
  riavvio.
- In un dato momento dovrebbe guidare la modalità controllo un solo client: le scritture
  concorrenti non vengono arbitrate.
- Un valore diverso da zero significa premuto; se serve il debounce è compito dell'host.
- Le risposte sono best effort: se la coda delle notifiche del dispositivo è
  satura, una risposta può essere scartata, quindi l'host deve prevedere un timeout.
