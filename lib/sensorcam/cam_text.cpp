#include "cam_text.h"

#include <string.h>

static int textLanguage = CAM_LANG_EN;

void camSetTextLanguage(int lang) { textLanguage = lang == CAM_LANG_PT ? CAM_LANG_PT : CAM_LANG_EN; }
int camTextLanguage() { return textLanguage; }

// Each phrase of the messages, in English and in European Portuguese. A test checks that every phrase the messages use
// is here, and that each has the same {n} as its English.
struct CamPhrase {
  const char* en;
  const char* pt;
};
static const CamPhrase PHRASES[] = {
    // Events, as their notifications say them.
    {"{0}: {1}", "{0}: {1}"},
    {"{0}: {1}, because the NVR is not listening", "{0}: {1}, porque o NVR não está a ouvir"},
    {"The NVR is not listening to {0}: alerts are not reaching it", "O NVR não está a ouvir {0}: os alertas não lhe chegam"},
    {"The NVR is listening to {0} again", "O NVR está a ouvir {0} outra vez"},
    {"{0} started ({1})", "{0} arrancou ({1})"},
    {"{0} started ({1}), after being off for up to {2}", "{0} arrancou ({1}), depois de desligada até {2}"},
    {"{0} was off its Wi-Fi network for up to {1}; alerts in that time reach the NVR as past events once it is back",
     "{0} esteve fora da rede Wi-Fi até {1}; os alertas desse período chegam ao NVR como eventos passados quando voltar"},
    {"an unknown time", "um tempo desconhecido"},
    {"unknown", "desconhecido"},
    {"{0} was armed", "{0} foi armada"},
    {"{0} was armed from {1}", "{0} foi armada a partir de {1}"},
    {"{0} was armed home: interior sensors are logged but not sent to the NVR",
     "{0} foi armada em casa: os sensores interiores ficam registados mas não são enviados ao NVR"},
    {"{0} was armed home from {1}: interior sensors are logged but not sent to the NVR",
     "{0} foi armada em casa a partir de {1}: os sensores interiores ficam registados mas não são enviados ao NVR"},
    {"{0} was disarmed: alerts are logged but not sent to the NVR",
     "{0} foi desarmada: os alertas ficam registados mas não são enviados ao NVR"},
    {"{0} was disarmed from {1}: alerts are logged but not sent to the NVR",
     "{0} foi desarmada a partir de {1}: os alertas ficam registados mas não são enviados ao NVR"},
    {"{0} has restarted by itself {1} times in an hour: something keeps stopping it, so alerts may be missed",
     "{0} reiniciou sozinha {1} vezes numa hora: algo a está a parar, por isso podem perder-se alertas"},
    {"{0}: the node restarted by a fault, such as a brownout from a weak battery or a bad wire; see the Nodes and pairing page",
     "{0}: o nó reiniciou por uma falha, como uma quebra de tensão por bateria fraca ou um fio com mau contacto; veja a página Nós e "
     "emparelhamento"},
    {"{0}: the battery will run out within a week", "{0}: a bateria vai acabar dentro de uma semana"},
    {"{0}: {1} nodes stopped reporting at the same time. Batteries fail one at a time, so something may be blocking the radio, such as a "
     "jammer",
     "{0}: {1} nós deixaram de comunicar ao mesmo tempo. As baterias falham uma de cada vez, por isso algo pode estar a bloquear o rádio, "
     "como um bloqueador"},
    {"{0}: fired {1} times within an hour. Every alert still goes to the NVR, but check the sensor's aim and wiring",
     "{0}: disparou {1} vezes numa hora. Todos os alertas continuam a ir para o NVR, mas verifique a orientação e as ligações do sensor"},
    {"Test from {0}", "Teste de {0}"},
    // The events' names, inside the messages.
    {"alert", "alerta"},
    {"delayed alert", "alerta atrasado"},
    {"clear", "fim"},
    {"test alert", "alerta de teste"},
    {"alert in quiet hours", "alerta em horas de silêncio"},
    {"alert while disarmed", "alerta com a central desarmada"},
    {"alert while armed home", "alerta com a central armada em casa"},
    {"alert not delivered", "alerta não entregue"},
    {"delayed alert not delivered", "alerta atrasado não entregue"},
    {"went silent", "ficou em silêncio"},
    {"low battery", "bateria fraca"},
    {"weak signal", "sinal fraco"},
    {"battery running out", "bateria a acabar"},
    {"node restarted by a fault", "nó reiniciou por uma falha"},
    {"firing often", "a disparar muitas vezes"},
    {"NVR not listening", "NVR não está a ouvir"},
    {"NVR listening again", "NVR a ouvir outra vez"},
    {"hub started", "central arrancou"},
    {"network lost", "rede perdida"},
    {"armed", "armada"},
    {"restarting often", "a reiniciar muitas vezes"},
    {"possible jamming", "possível interferência"},
    {"silent", "em silêncio"},
    {"messages refused", "mensagens recusadas"},
    // Who armed or disarmed, and for how long.
    {"the web page", "a página web"},
    {"Home Assistant", "Home Assistant"},
    {"the re-arm timer", "o temporizador de rearme"},
    {"{0}, for {1}", "{0}, durante {1}"},
    // Lengths of time.
    {"{0} min", "{0} min"},
    {"{0} h", "{0} h"},
    {"{0} h {1} min", "{0} h {1} min"},
    {"{0} day", "{0} dia"},
    {"{0} days", "{0} dias"},
    {"{0} days {1} h", "{0} dias {1} h"},
    // Why the hub started.
    {"Power on or reset button", "ligação à corrente ou botão de reinício"},
    {"Crash", "falha"},
    {"Watchdog", "watchdog"},
    {"Low power supply voltage", "tensão de alimentação baixa"},
    {"Other", "outro"},
    {"Software restart", "reinício por software"},
    {"WiFi lost for 5 minutes", "Wi-Fi perdido durante 5 minutos"},
    {"Low memory", "pouca memória"},
    {"Server stalled", "o servidor parou"},
    {"Router stopped answering", "o router deixou de responder"},
    {"New WiFi settings", "novas definições de Wi-Fi"},
    {"New web, port or address settings", "novas definições web, de porta ou de endereço"},
    {"Fixed address could not reach the router", "o endereço fixo não chegou ao router"},
    {"Scheduled restart", "reinício programado"},
    {"Settings reset", "definições repostas"},
    {"Restart button", "botão de reinício"},
    {"Firmware update", "atualização do firmware"},
    {"Settings restored", "definições restauradas"},
    {"New keys", "novas chaves"},
    // The daily check and the weekly summary.
    {"{0} daily check: {1} of {2} nodes heard in the last day", "{0} verificação diária: {1} de {2} nós ouvidos no último dia"},
    {"no problems", "sem problemas"},
    {"problems: {0}", "problemas: {0}"},
    {"{0} alert in the last day", "{0} alerta no último dia"},
    {"{0} alerts in the last day", "{0} alertas no último dia"},
    {"at least {0} alert in the last day", "pelo menos {0} alerta no último dia"},
    {"at least {0} alerts in the last day", "pelo menos {0} alertas no último dia"},
    {"{0} not delivered to the NVR", "{0} não entregues ao NVR"},
    {"all delivered to the NVR", "todos entregues ao NVR"},
    {"the NVR is listening", "o NVR está a ouvir"},
    {"the NVR is NOT listening", "o NVR NÃO está a ouvir"},
    {"disarmed", "desarmada"},
    {"armed away", "armada ausente"},
    {"armed home", "armada em casa"},
    {"last test alert {0} ago", "último alerta de teste há {0}"},
    {"last test alert {0} ago, over a month: send one from the hub's Overview, to see that the NVR still records",
     "último alerta de teste há {0}, mais de um mês: envie um no Resumo da central, para ver que o NVR continua a gravar"},
    {"no test alert in the last month: send one from the hub's Overview, to see that the NVR still records",
     "nenhum alerta de teste no último mês: envie um no Resumo da central, para ver que o NVR continua a gravar"},
    {"{0} weekly summary", "{0} resumo semanal"},
    {"{0} alert in the last 7 days", "{0} alerta nos últimos 7 dias"},
    {"{0} alerts in the last 7 days", "{0} alertas nos últimos 7 dias"},
    {"at least {0} alert in the last 7 days", "pelo menos {0} alerta nos últimos 7 dias"},
    {"at least {0} alerts in the last 7 days", "pelo menos {0} alertas nos últimos 7 dias"},
    {"most from {0} ({1})", "mais de {0} ({1})"},
    {"{0} more only logged, while disarmed, armed home or in quiet hours",
     "mais {0} só registados, com a central desarmada, armada em casa ou em horas de silêncio"},
    {"{0} problem reported", "{0} problema comunicado"},
    {"{0} problems reported", "{0} problemas comunicados"},
    {"batteries running low: {0}", "baterias a acabar: {0}"},
    {"{0}: about {1} day", "{0}: cerca de {1} dia"},
    {"{0}: about {1} days", "{0}: cerca de {1} dias"},
    {"no problems now", "sem problemas agora"},
    {"problems now: {0}", "problemas agora: {0}"},
    {"no test alert this week", "nenhum alerta de teste esta semana"},
};

const char* camText(const char* en) {
  if (textLanguage != CAM_LANG_PT) return en;
  for (const CamPhrase& p : PHRASES)
    if (strcmp(p.en, en) == 0) return p.pt;
  return en;
}

std::string camText(const char* en, std::initializer_list<std::string> values) {
  std::string out;
  for (const char* s = camText(en); *s; s++) {
    if (*s == '{' && s[1] >= '0' && s[1] <= '9' && s[2] == '}') {
      size_t i = (size_t)(s[1] - '0');
      if (i < values.size()) out += *(values.begin() + i);
      s += 2;
    } else {
      out += *s;
    }
  }
  return out;
}

std::string camCount(long long n, const char* one, const char* many) { return camText(n == 1 ? one : many, {std::to_string(n)}); }

// For the test: every phrase, so it can check each has its {n} in both languages.
size_t camPhraseCount() { return sizeof(PHRASES) / sizeof(PHRASES[0]); }
const char* camPhraseEnglish(size_t i) { return PHRASES[i].en; }
const char* camPhrasePortuguese(size_t i) { return PHRASES[i].pt; }
