// The pages' words in European Portuguese, each by its English, as i18n.js uses them. tools/check_translations.py
// fails while a phrase of the pages is missing here, or one here is used no more.
const PT = {
  // ---- the pages' own text, in index.html and setup.html ----
  ", from the node project, one for each kind of chip. Each node of that chip running another version fetches it by itself after its next report, unless it is saving its battery; one whose update fails goes back to what it had, and does not try that file again. A node watches nothing while it updates, for a minute or so, and stops to report if its sensor fires.":
    ", do projeto do nó, um para cada tipo de chip. Cada nó desse chip com outra versão vai buscá-lo sozinho depois da sua próxima comunicação, a não ser que esteja a poupar a bateria; um cuja atualização falhe volta ao que tinha e não tenta esse ficheiro outra vez. Um nó não vigia nada enquanto se atualiza, durante cerca de um minuto, e para para comunicar se o seu sensor disparar.",
  ". The hub takes only firmware signed for it, and keeps what it has if the file is anything else. The video stops until it has restarted, about a minute.":
    ". A central só aceita firmware assinado para ela, e mantém o que tem se o ficheiro for outra coisa. O vídeo para até ela reiniciar, cerca de um minuto.",
  "8 characters or more. A few words are easier to keep than a jumble of symbols. Restoring a backup asks for the one it was sealed with.":
    "8 caracteres ou mais. Algumas palavras guardam-se melhor do que uma mistura de símbolos. Restaurar uma cópia pede a frase com que foi selada.",
  "8 to 63 characters": "8 a 63 caracteres",
  "A backup is sealed with a passphrase you choose, so the file holds no password or key anyone can read. Keep the passphrase apart from the file: without it, nobody can open the backup, you included.":
    "Uma cópia de segurança é selada com uma frase-passe à sua escolha, por isso o ficheiro não tem nenhuma palavra-passe ou chave que alguém consiga ler. Guarde a frase-passe longe do ficheiro: sem ela, ninguém consegue abrir a cópia, nem o próprio.",
  "A disarm from Home Assistant lasts": "Um desarme feito no Home Assistant dura",
  "A new ESP-NOW key, pairing key, sensor token and snapshot address, then a restart. Every node must be set up or paired again, HTTP sensors need the new token, and ONVIF clients the new snapshot address. Use it if a key may have got out.":
    "Uma nova chave ESP-NOW, chave de emparelhamento, token dos sensores e endereço de imagens, e depois um reinício. Todos os nós têm de ser configurados ou emparelhados outra vez, os sensores HTTP precisam do novo token e os clientes ONVIF do novo endereço de imagens. Use-o se uma chave possa ter sido divulgada.",
  "A node that supports pairing finds the hub by itself and gets a key of its own. Allow pairing, start the node in pairing mode, check that the fingerprint shown here matches the one on the node's label, and approve it. Its sensors are added for you.":
    "Um nó que suporte emparelhamento encontra a central sozinho e recebe uma chave própria. Permita o emparelhamento, ligue o nó em modo de emparelhamento, confirme que a impressão digital mostrada aqui é igual à da etiqueta do nó, e aprove-o. Os seus sensores são adicionados automaticamente.",
  "A node's new": "O novo firmware de um nó,",
  "A picture of the property with each sensor where it is: red while it fires, amber with a problem, grey until it is first heard; or coloured by how well the hub hears each one, to see where the signal is weak. Any picture will do, such as a photo of a drawing or a screenshot of a map; it is made small enough for the hub. The walk test shows it too.":
    "Uma imagem da propriedade com cada sensor no seu lugar: vermelho enquanto dispara, âmbar com um problema, cinzento até ser ouvido pela primeira vez; ou colorido pela qualidade com que a central ouve cada um, para ver onde o sinal é fraco. Serve qualquer imagem, como uma fotografia de um desenho ou uma captura de um mapa; é reduzida para caber na central. O teste de passagem também a mostra.",
  "A sensor on the plan": "Um sensor na planta",
  "A small file of the settings and each paired node's key. Pictures are not included.":
    "Um ficheiro pequeno com as definições e a chave de cada nó emparelhado. As imagens não estão incluídas.",
  "Add a node": "Adicionar um nó",
  "Add a sensor": "Adicionar um sensor",
  "Add node": "Adicionar nó",
  Address: "Endereço",
  Addresses: "Endereços",
  Again: "Outra vez",
  "Again, to download": "Outra vez, para descarregar",
  Alarm: "Alarme",
  "Alarm hub": "Central de alarme",
  "Alert settings and pictures": "Definições de alerta e imagens",
  Alerts: "Alertas",
  "All clear picture": "Imagem de tudo calmo",
  "Allow new nodes to pair for 2 minutes": "Permitir que novos nós emparelhem durante 2 minutos",
  "Also the NVR's login for this camera. A new port or name restarts the hub; a new password or other names apply at once.":
    "É também o acesso do NVR a esta câmara. Uma nova porta ou nome reinicia a central; uma nova palavra-passe ou outros nomes aplicam-se de imediato.",
  "An ntfy or Gotify server on your network passes each message on to your phone; a Home Assistant webhook or Node-RED can act on it. Use http:// and an IP address: https and names are not supported, so a service on the internet needs a server on your network in between.":
    "Um servidor ntfy ou Gotify na sua rede passa cada mensagem ao seu telemóvel; um webhook do Home Assistant ou o Node-RED podem agir sobre ela. Use http:// e um endereço IP: https e nomes não são suportados, por isso um serviço na internet precisa de um servidor na sua rede pelo meio.",
  "Arm away": "Armar ausente",
  "Arm home": "Armar em casa",
  "Backup and restore": "Cópia de segurança e restauro",
  "Backup network": "Rede de reserva",
  Battery: "Bateria",
  "Broker address": "Endereço do broker",
  "Changes not saved": "Alterações por guardar",
  "Choose a password for the hub.": "Escolha uma palavra-passe para a central.",
  "Choose a picture": "Escolher uma imagem",
  Connection: "Ligação",
  Connections: "Ligações",
  "Create missing thumbnails": "Criar as miniaturas em falta",
  "Current time": "Hora atual",
  "DNS server": "Servidor DNS",
  "Daily check at": "Verificação diária às",
  "Danger zone": "Zona de perigo",
  "Date and time": "Data e hora",
  Day: "Dia",
  "Daylight saving": "Hora de verão",
  "Delete the plan": "Apagar a planta",
  "Device info": "Informação do aparelho",
  Disarm: "Desarmar",
  Discard: "Descartar",
  "Download backup": "Descarregar cópia",
  "Download full backup": "Descarregar cópia completa",
  "Download the full history (CSV)": "Descarregar o histórico completo (CSV)",
  "Drop a full backup": "Arraste uma cópia completa",
  "Drop a node's": "Arraste o firmware de um nó,",
  "Drop a settings backup here, or": "Arraste para aqui uma cópia das definições, ou",
  "Drop the firmware": "Arraste o firmware",
  "Each step is ticked as the hub sees it done.": "Cada passo fica marcado quando a central o vê feito.",
  "Empty for an open or already saved network": "Vazio para uma rede aberta ou já guardada",
  Enabled: "Ativo",
  "Erases the saved Wi-Fi networks, ports, name, hub password, fixed address, time, notification and MQTT settings, and arms the hub. Pictures, sensor nodes, and the ESP-NOW key and token that sensors use, are kept.":
    "Apaga as redes Wi-Fi guardadas, as portas, o nome, a palavra-passe da central, o endereço fixo, a hora e as definições de notificações e MQTT, e arma a central. As imagens, os nós sensores, e a chave ESP-NOW e o token que os sensores usam, são mantidos.",
  Europe: "Europa",
  Events: "Eventos",
  "Every day": "Todos os dias",
  "Firmware update": "Atualização do firmware",
  "Floor plan": "Planta",
  "Forget the saved token": "Esquecer o token guardado",
  Friday: "Sexta-feira",
  "Full backup": "Cópia completa",
  Gateway: "Gateway",
  "Hide for now": "Esconder por agora",
  "Home Assistant, Node-RED or other (JSON)": "Home Assistant, Node-RED ou outro (JSON)",
  "How long a disarm lasts": "Quanto tempo dura um desarme",
  "Hub Wi-Fi setup": "Configuração Wi-Fi da central",
  "Hub password": "Palavra-passe da central",
  "IP address": "Endereço IP",
  "IP mode": "Modo IP",
  "Its first hour": "A sua primeira hora",
  "Its latest events": "Os seus últimos eventos",
  "Its sensors, and when they fired over the last 7 days": "Os seus sensores, e quando dispararam nos últimos 7 dias",
  "Leave it empty to turn MQTT off.": "Deixe vazio para desligar o MQTT.",
  "Left blank, the saved one is kept. Give the broker a login: without one, the hub takes no disarm from Home Assistant, as anything on the network could send it. The connection is not encrypted, as with the hub's other connections.":
    "Em branco, mantém-se a guardada. Dê um acesso ao broker: sem ele, a central não aceita desarmes do Home Assistant, porque qualquer coisa na rede os poderia enviar. A ligação não é cifrada, como as outras ligações da central.",
  "Live video": "Vídeo em direto",
  "Log out": "Sair",
  "Looking for networks...": "À procura de redes...",
  "MAC address": "Endereço MAC",
  "MAC address of a node to add": "Endereço MAC de um nó a adicionar",
  "MQTT broker": "Broker MQTT",
  "Main network": "Rede principal",
  Maintenance: "Manutenção",
  "Make new keys": "Criar novas chaves",
  Manual: "Manual",
  Monday: "Segunda-feira",
  "NTP server": "Servidor NTP",
  Name: "Nome",
  Network: "Rede",
  "No NVR address is set, so the hub cannot tell whether your NVR recorded an alert: any device subscribed to its events counts. Enter it under Recording > NVR.":
    "Não há endereço do NVR, por isso a central não sabe se o seu NVR gravou um alerta: conta qualquer aparelho inscrito nos seus eventos. Indique-o em Gravação > NVR.",
  Node: "Nó",
  "Node firmware": "Firmware dos nós",
  "Node problems: silent, low battery, weak signal, possible jamming, a sensor firing often":
    "Problemas dos nós: em silêncio, bateria fraca, sinal fraco, possível interferência, um sensor a disparar muitas vezes",
  "Nodes and pairing": "Nós e emparelhamento",
  None: "Nenhum",
  Notifications: "Notificações",
  "ONVIF port": "Porta ONVIF",
  "One message a day at this local time: the nodes heard, any problems, the day's alerts and whether they reached the NVR, and your last test alert, with a reminder to send one when the last is over a month old. If it stops arriving, the hub is off or cannot reach the address. Leave it empty for none; it needs the hub's clock.":
    "Uma mensagem por dia a esta hora local: os nós ouvidos, os problemas, os alertas do dia e se chegaram ao NVR, e o seu último alerta de teste, com um lembrete para enviar um quando o último tiver mais de um mês. Se deixar de chegar, a central está desligada ou não consegue chegar ao endereço. Deixe vazio para nenhuma; precisa do relógio da central.",
  "Open this on your phone and walk the property. Every sensor that sees you shows here, with how strong its signal is and how long the hub took to hear it, and each node's light blinks green, yellow or red for its signal. Sensors still waiting at the end may not cover where you walked. The walk test lasts 10 minutes; alerts still reach the NVR meanwhile.":
    "Abra isto no telemóvel e percorra a propriedade. Cada sensor que o vir aparece aqui, com a força do seu sinal e o tempo que a central levou a ouvi-lo, e a luz de cada nó pisca a verde, amarelo ou vermelho conforme o sinal. Os sensores ainda à espera no fim podem não cobrir por onde passou. O teste de passagem dura 10 minutos; entretanto os alertas continuam a chegar ao NVR.",
  "Or type its name": "Ou escreva o nome",
  "Other (plain text)": "Outro (texto simples)",
  "Other names": "Outros nomes",
  Overview: "Resumo",
  Pages: "Páginas",
  Pairing: "Emparelhamento",
  Passphrase: "Frase-passe",
  Password: "Palavra-passe",
  "Password (user admin)": "Palavra-passe (utilizador admin)",
  Port: "Porta",
  "Put on the hub": "Enviar para a central",
  "RTSP port": "Porta RTSP",
  "Raise an NVR alarm when a node goes silent or its battery runs low, or several nodes stop reporting together":
    "Dar um alarme no NVR quando um nó fica em silêncio ou com a bateria fraca, ou vários nós deixam de comunicar ao mesmo tempo",
  "Read from this PC": "Ler deste PC",
  "Ready to protect?": "Pronta para proteger?",
  Recording: "Gravação",
  "Reset settings": "Repor definições",
  "Reset the settings": "Repor as definições",
  "Restart the hub": "Reiniciar a central",
  "Restore full backup": "Restaurar cópia completa",
  "Restore settings": "Restaurar definições",
  "Right now": "Agora mesmo",
  Saturday: "Sábado",
  Save: "Guardar",
  "Scheduled restart": "Reinício programado",
  "Send a test": "Enviar um teste",
  "Send a test alert": "Enviar um alerta de teste",
  "Send the weekly summary now": "Enviar já o resumo semanal",
  "Send to": "Enviar para",
  Sensor: "Sensor",
  "Sensor nodes": "Nós sensores",
  Sensors: "Sensores",
  "Set time by": "Acertar a hora por",
  "Setting up": "Configuração",
  Settings: "Definições",
  Show: "Mostrar",
  "Show how well the hub hears each one": "Mostrar como a central ouve cada um",
  "Show more": "Mostrar mais",
  "Show the file names, as the file manager and downloads use them":
    "Mostrar os nomes dos ficheiros, como os usam o gestor de ficheiros e as transferências",
  "Show the steps one at a time": "Mostrar os passos um de cada vez",
  "Show what each sensor is doing": "Mostrar o que cada sensor está a fazer",
  "Skip to the page": "Saltar para a página",
  "Start on every node": "Iniciar em todos os nós",
  Static: "Fixo",
  Status: "Estado",
  Stop: "Parar",
  "Subnet mask": "Máscara de sub-rede",
  Sunday: "Domingo",
  System: "Sistema",
  "Take it off the plan": "Tirar da planta",
  "Test everything": "Testar tudo",
  "The NVR": "O NVR",
  "The NVR always receives UTC, so these settings only change the time shown here and the scheduled restart. After a power cut, a manual time is read once from the NTP server if the hub can reach it; otherwise set it again.":
    "O NVR recebe sempre UTC, por isso estas definições só mudam a hora mostrada aqui e o reinício programado. Depois de um corte de energia, uma hora manual é lida uma vez do servidor NTP se a central lhe chegar; caso contrário, acerte-a de novo.",
  "The NVR's channel for the hub uses this port. A new port restarts the hub, and the NVR loses the camera until its channel has the new one.":
    "O canal do NVR para a central usa esta porta. Uma nova porta reinicia a central, e o NVR perde a câmara até o canal ter a nova.",
  "The hub and each sensor appear in Home Assistant by themselves, through an MQTT broker: in Home Assistant, install the Mosquitto broker add-on and the MQTT integration, then enter the broker here. Home Assistant shows the hub as unavailable as soon as it loses its connection, so an automation can warn when the hub is unplugged or kept off the network.":
    "A central e cada sensor aparecem sozinhos no Home Assistant, através de um broker MQTT: no Home Assistant, instale o extra Mosquitto broker e a integração MQTT, e depois indique aqui o broker. O Home Assistant mostra a central como indisponível assim que perde a ligação, por isso uma automação pode avisar quando a central é desligada ou afastada da rede.",
  "The hub answers only to its address, its name, and these names, such as one your router gives it, separated by commas. That stops a website from reaching it through your browser.":
    "A central só responde ao seu endereço, ao seu nome e a estes nomes, como um que o router lhe dê, separados por vírgulas. Isso impede um site de lhe chegar através do seu navegador.",
  "The hub is disarmed: alerts are logged but not sent to the NVR, so nothing is recorded. Arm it on the Overview page.":
    "A central está desarmada: os alertas ficam registados mas não são enviados ao NVR, por isso nada é gravado. Arme-a na página Resumo.",
  "The hub password again": "A palavra-passe da central outra vez",
  "The hub's clock": "O relógio da central",
  "The hub's pages": "As páginas da central",
  "The node being set up": "O nó em configuração",
  "The picture the hub is sending": "A imagem que a central está a enviar",
  "The restart waits while an alert is on, and is skipped until the next time if the alert lasts over 30 minutes.":
    "O reinício espera enquanto houver um alerta, e fica para a vez seguinte se o alerta durar mais de 30 minutos.",
  "The sensor nodes talk to the hub over ESP-NOW, a radio link of their own beside Wi-Fi.":
    "Os nós sensores falam com a central por ESP-NOW, uma ligação de rádio própria ao lado do Wi-Fi.",
  "The settings and node keys, sealed, with every picture, and each sensor's name and alert settings, in one file made by this browser. Restoring adds the sensors and pictures in it, replacing any with the same name, then replaces the settings and restarts the hub.":
    "As definições e as chaves dos nós, seladas, com todas as imagens, e o nome e as definições de alerta de cada sensor, num só ficheiro feito por este navegador. Restaurar acrescenta os sensores e as imagens que tiver, substituindo os que tenham o mesmo nome, depois substitui as definições e reinicia a central.",
  "The video stops, and the NVR records nothing, until it has started again.":
    "O vídeo para, e o NVR não grava nada, até ela arrancar de novo.",
  "The video's port, for the NVR and for viewers such as VLC. A new port restarts the hub.":
    "A porta do vídeo, para o NVR e para leitores como o VLC. Uma nova porta reinicia a central.",
  "The week's alerts and whether they reached the NVR, the sensor that fired most, the problems, batteries forecast to run out within a month, and the last test alert.":
    "Os alertas da semana e se chegaram ao NVR, o sensor que mais disparou, os problemas, as baterias que devem acabar dentro de um mês, e o último alerta de teste.",
  "This hub has no password yet. Choose one: it guards the hub's page, the NVR's login and this setup network.":
    "Esta central ainda não tem palavra-passe. Escolha uma: protege a página da central, o acesso do NVR e esta rede de configuração.",
  Thursday: "Quinta-feira",
  Time: "Hora",
  Timezone: "Fuso horário",
  Token: "Token",
  Tuesday: "Terça-feira",
  "United States": "Estados Unidos",
  "Unknown senders": "Remetentes desconhecidos",
  "Update firmware": "Atualizar o firmware",
  "Use as": "Usar como",
  "Use the": "Use o",
  User: "Utilizador",
  "Walk test": "Teste de passagem",
  Wednesday: "Quarta-feira",
  "Weekly summary": "Resumo semanal",
  "What still needs doing before the hub can be trusted. This list goes once everything is done, and comes back if something slips.":
    "O que ainda falta antes de se poder confiar na central. Esta lista desaparece quando tudo estiver feito, e volta se algo falhar.",
  "What the spots show": "O que os pontos mostram",
  "What to send": "O que enviar",
  "When it fired, the last 7 days": "Quando disparou, nos últimos 7 dias",
  "Where the hub is on your network. DHCP lets the router choose; a fixed address stays the same, so the NVR always finds the hub.":
    "Onde está a central na sua rede. Com DHCP escolhe o router; um endereço fixo mantém-se, para o NVR encontrar sempre a central.",
  "Where to send": "Para onde enviar",
  "Which events": "Que eventos",
  "Which sensor": "Que sensor",
  "Wi-Fi settings": "Definições de Wi-Fi",
  "With it, the hub checks that the NVR listens and records each alert, and plays again any alert it missed.":
    "Com ele, a central confirma que o NVR ouve e grava cada alerta, e repete qualquer alerta que tenha perdido.",
  "Without one, anyone on this network can disarm it, change it and watch its video. It is also the NVR's login for this camera, so enter it on the NVR's channel too. The hub restarts to use it.":
    "Sem ela, qualquer pessoa nesta rede pode desarmá-la, alterá-la e ver o seu vídeo. É também o acesso do NVR a esta câmara, por isso indique-a também no canal do NVR. A central reinicia para a usar.",
  "a node, or a device that calls a web address": "um nó, ou um aparelho que chama um endereço web",
  "a test alert along the whole path, checked step by step":
    "um alerta de teste por todo o caminho, verificado passo a passo",
  alerts: "alertas",
  at: "às",
  "choose it": "escolha-o",
  "every event": "todos os eventos",
  "every sensor and the hub": "todos os sensores e a central",
  "for this board from a release, the one without": "desta placa, de uma versão publicada, o que não tem",
  "here, or": "para aqui, ou",
  problems: "problemas",
  "the hub's own": "os da própria central",

  // ---- the hub's messages, as it sends them ----
  "Gotify needs the application's token": "O Gotify precisa do token da aplicação",
  "NVR listening again": "NVR a ouvir outra vez",
  "NVR not listening": "NVR não está a ouvir",
  "a backup is sealed with a key from the page; reload the page and try again":
    "uma cópia é selada com uma chave feita pela página; recarregue a página e tente outra vez",
  "a disarm lasts 0 to 10080 minutes": "um desarme dura de 0 a 10080 minutos",
  "alert in quiet hours": "alerta em horas de silêncio",
  "alert not delivered": "alerta não entregue",
  "alert while armed home": "alerta com a central armada em casa",
  "alert while disarmed": "alerta com a central desarmada",
  armed: "armada",
  "armed home": "armada em casa",
  "battery running out": "bateria a acabar",
  "check the IP address": "verifique o endereço IP",
  "check the address: the gateway must be on the same network, and the address cannot be the network or broadcast address":
    "verifique o endereço: o gateway tem de estar na mesma rede, e o endereço não pode ser o da rede nem o de difusão",
  "check the day and the time": "verifique o dia e a hora",
  "check the timezone, the NTP server name and the date and time":
    "verifique o fuso horário, o nome do servidor NTP e a data e hora",
  "choose a hub password of 8 to 63 characters": "escolha uma palavra-passe da central com 8 a 63 caracteres",
  "could not open that backup: the passphrase is wrong, or the file is damaged":
    "não foi possível abrir essa cópia: a frase-passe está errada, ou o ficheiro está danificado",
  "could not restore that file: it is damaged": "não foi possível restaurar esse ficheiro: está danificado",
  "could not restore that file: it is damaged, or the hub's settings storage is full":
    "não foi possível restaurar esse ficheiro: está danificado, ou o espaço de definições da central está cheio",
  "could not save the file; storage may be full":
    "não foi possível guardar o ficheiro; o armazenamento pode estar cheio",
  "could not save the hub password": "não foi possível guardar a palavra-passe da central",
  "could not save the settings": "não foi possível guardar as definições",
  "days must be 1 to 3650": "os dias têm de ser de 1 a 3650",
  "delayed alert not delivered": "alerta atrasado não entregue",
  disarmed: "desarmada",
  "enter an address like http://192.168.1.10:8123/path, with an IP address, not a name, and http, not https":
    "indique um endereço como http://192.168.1.10:8123/caminho, com um endereço IP, não um nome, e http, não https",
  "enter the broker's IP address, such as 192.168.1.10; names are not supported":
    "indique o endereço IP do broker, como 192.168.1.10; os nomes não são suportados",
  "file is larger than 32 KB": "o ficheiro tem mais de 32 KB",
  "firing often": "a disparar muitas vezes",
  "for ntfy, end the address with the topic, as in http://192.168.1.10/alarm":
    "para o ntfy, termine o endereço com o tópico, como em http://192.168.1.10/alarme",
  "give the node's MAC address, as AA-BB-CC-DD-EE-FF": "indique o endereço MAC do nó, como AA-BB-CC-DD-EE-FF",
  "give the sensor, its pin, and the tamper switch's and battery's pins, empty for none, and the battery's kind":
    "indique o sensor, o seu pino, e os pinos do interruptor antissabotagem e da bateria, vazios para nenhum, e o tipo de bateria",
  "hub started": "central arrancou",
  "invalid path": "caminho inválido",
  "low battery": "bateria fraca",
  "mode must be away, home or disarmed": "o modo tem de ser ausente, em casa ou desarmado",
  "network lost": "rede perdida",
  "no crash report is saved": "não há nenhum relatório de falha guardado",
  "no such file": "esse ficheiro não existe",
  "no such node": "esse nó não existe",
  "no such sensor": "esse sensor não existe",
  "node restarted by a fault": "nó reiniciou por uma falha",
  "not a JPEG": "não é um JPEG",
  "not a single H.264 picture with SPS, PPS and IDR": "não é uma só imagem H.264 com SPS, PPS e IDR",
  "not confirmed": "não confirmado",
  "not found": "não encontrado",
  "nothing to delete at that path": "nada para apagar nesse caminho",
  "other names: up to 4, separated by commas, of letters, digits, dots and hyphens":
    "outros nomes: até 4, separados por vírgulas, com letras, algarismos, pontos e hífenes",
  "pairing is not available: the hub has no pairing key":
    "o emparelhamento não está disponível: a central não tem chave de emparelhamento",
  "ports must be 1 to 65535 and all different": "as portas têm de ser de 1 a 65535 e todas diferentes",
  "possible jamming": "possível interferência",
  "restarting often": "a reiniciar muitas vezes",
  "save an address first": "guarde primeiro um endereço",
  "storage is nearly full, and the rest is kept for the event history; delete some pictures first":
    "o armazenamento está quase cheio, e o resto fica para o histórico de eventos; apague primeiro algumas imagens",
  "test alert": "alerta de teste",
  "text is too long": "o texto é demasiado longo",
  "that firmware is not signed for this hub, so nothing was changed":
    "esse firmware não está assinado para esta central, por isso nada mudou",
  "that firmware is older than this one, so nothing was changed; an older one goes on only over the cable":
    "esse firmware é mais antigo do que este, por isso nada mudou; um mais antigo só entra pelo cabo",
  "that is not a node of this chip": "isso não é um nó deste chip",
  "that is not a sensor node's firmware: use the firmware.bin of the node project, built for the node's chip":
    "isso não é firmware de um nó sensor: use o firmware.bin do projeto do nó, feito para o chip do nó",
  "that is not valid firmware for this hub, so nothing was changed":
    "isso não é firmware válido para esta central, por isso nada mudou",
  "that node firmware is not signed for these nodes, so nothing was changed: use a signed firmware.bin of the node project":
    "esse firmware de nó não está assinado para estes nós, por isso nada mudou: use um firmware.bin assinado do projeto do nó",
  "the camera's name must be up to 32 characters, on one line":
    "o nome da câmara tem de ter até 32 caracteres, numa só linha",
  "the daily check's time must be 0 to 1439 minutes after midnight, or -1 for none":
    "a hora da verificação diária tem de ser de 0 a 1439 minutos depois da meia-noite, ou -1 para nenhuma",
  "the format must be json, text, ntfy or gotify": "o formato tem de ser json, text, ntfy ou gotify",
  "the history is being downloaded already; try again when that is done":
    "o histórico já está a ser descarregado; tente outra vez quando acabar",
  "the hold must be 1 to 3600 seconds, quiet hours need a start and an end that differ, and the zone must be interior or perimeter":
    "a duração tem de ser de 1 a 3600 segundos, as horas de silêncio precisam de um início e um fim diferentes, e a zona tem de ser interior ou perímetro",
  "the hub always needs a password; enter a new one to change it":
    "a central precisa sempre de uma palavra-passe; indique uma nova para a mudar",
  "the hub is short of memory; try again": "a central tem pouca memória; tente outra vez",
  "the hub's clock is not set yet, and the node needs it to know when the walk test ends":
    "o relógio da central ainda não está acertado, e o nó precisa dele para saber quando acaba o teste de passagem",
  "the hub's clock is not set yet, so it cannot tell which days those are; try again":
    "o relógio da central ainda não está acertado, por isso não sabe que dias são esses; tente outra vez",
  "the name may use letters, digits and hyphens, up to 63 characters, and cannot start or end with a hyphen":
    "o nome pode ter letras, algarismos e hífenes, até 63 caracteres, e não pode começar nem acabar com um hífen",
  "the name must be 1 to 32 characters and the password empty or 8 to 63":
    "o nome tem de ter de 1 a 32 caracteres e a palavra-passe ficar vazia ou ter de 8 a 63",
  "the name must be up to 64 characters, on one line": "o nome tem de ter até 64 caracteres, numa só linha",
  "the password must be 8 to 63 characters": "a palavra-passe tem de ter de 8 a 63 caracteres",
  "the past-event time must be 1 to 86400 seconds, the silent time 60 to 604800, the report time 60 to 86400, and the repeat time 1 to 60":
    "o tempo de eventos passados tem de ser de 1 a 86400 segundos, o de silêncio de 60 a 604800, o de comunicação de 60 a 86400, e o de repetição de 1 a 60",
  "the port must be 1 to 65535": "a porta tem de ser de 1 a 65535",
  "the test is running already, or the hub is off its Wi-Fi network":
    "o teste já está a correr, ou a central está fora da sua rede Wi-Fi",
  "the token must be up to 64 letters, digits, dots, hyphens and underscores":
    "o token tem de ter até 64 letras, algarismos, pontos, hífenes e sublinhados",
  "the user name and password may have up to 64 characters":
    "o nome de utilizador e a palavra-passe podem ter até 64 caracteres",
  "the weekly summary's day must be 0 for Sunday to 6, or -1 for none":
    "o dia do resumo semanal tem de ser de 0, para domingo, a 6, ou -1 para nenhum",
  "the weekly summary's time must be 0 to 1439 minutes after midnight":
    "a hora do resumo semanal tem de ser de 0 a 1439 minutos depois da meia-noite",
  "there is no firmware for that chip": "não há firmware para esse chip",
  "this board has no radio test": "esta placa não tem teste de rádio",
  "this hub has no source of random numbers to seal a backup with":
    "esta central não tem uma fonte de números aleatórios para selar uma cópia",
  "unsupported request": "pedido não suportado",
  "weak signal": "sinal fraco",
  "went silent": "ficou em silêncio",
  "wrong video format: make it with tools/make_frames.py": "formato de vídeo errado: faça-o com tools/make_frames.py",

  // ---- the scripts' words ----
  // setup.js
  " (open)": " (aberta)",
  "Main network: {main}. Backup: {backup}.": "Rede principal: {main}. Reserva: {backup}.",
  none: "nenhuma",
  "No network saved yet.": "Ainda não há nenhuma rede guardada.",
  "Choose or type a network.": "Escolha ou escreva uma rede.",
  "Choose a hub password of 8 to 63 characters.": "Escolha uma palavra-passe da central com 8 a 63 caracteres.",
  "The two hub passwords differ.": "As duas palavras-passe da central são diferentes.",
  "Saved as the backup network.": "Guardada como rede de reserva.",
  "Saved. The hub is restarting to join {network}.": "Guardada. A central está a reiniciar para entrar em {network}.",
  "Reconnect this device to that network, then open {address}.":
    "Ligue este aparelho a essa rede e depois abra {address}.",
  "Reconnect this device to that network.": "Ligue este aparelho a essa rede.",
  "If neither saved network works, the setup network comes back in about a minute, with the hub password as its password.":
    "Se nenhuma rede guardada funcionar, a rede de configuração volta dentro de cerca de um minuto, com a palavra-passe da central como palavra-passe.",
  "If neither saved network works, the setup network comes back in about a minute.":
    "Se nenhuma rede guardada funcionar, a rede de configuração volta dentro de cerca de um minuto.",
  "The hub did not answer. Try again.": "A central não respondeu. Tente outra vez.",
  // index.js
  Language: "Idioma",
  "Armed away": "Armada ausente",
  "Armed home": "Armada em casa",
  Disarmed: "Desarmada",
  "A hub password of your own, not one built into the firmware":
    "Uma palavra-passe da central só sua, não uma que venha no firmware",
  "Choose a password": "Escolher uma palavra-passe",
  "The hub password guards this page, the NVR's login and the hub's setup network. Choose one of 8 to 63 characters that only you know; the NVR needs it too, so note it down.":
    "A palavra-passe da central protege esta página, o acesso do NVR e a rede de configuração da central. Escolha uma com 8 a 63 caracteres que só o próprio conheça; o NVR também precisa dela, por isso aponte-a.",
  "The hub's clock is set": "O relógio da central está acertado",
  "Set the clock": "Acertar o relógio",
  "The hub needs the time to stamp each alert, keep quiet hours and send the daily check. With internet access it sets itself; otherwise give it a time server on your network, such as the router.":
    "A central precisa da hora para marcar cada alerta, respeitar as horas de silêncio e enviar a verificação diária. Com acesso à internet acerta-se sozinha; caso contrário, dê-lhe um servidor de hora na sua rede, como o router.",
  "The NVR's address is set": "O endereço do NVR está indicado",
  "Enter the NVR's address": "Indicar o endereço do NVR",
  "With the NVR's address, the hub can check that the NVR listens and that each alert was recorded, and play again any it missed.":
    "Com o endereço do NVR, a central pode confirmar que o NVR está a ouvir e que cada alerta foi gravado, e repetir os que tenha perdido.",
  "The NVR is listening": "O NVR está a ouvir",
  "Open the NVR page": "Abrir a página do NVR",
  "On the NVR, add the hub as an ONVIF camera on a free channel: its address, port 8899, user admin and the hub password. Then turn on motion detection with recording for that channel. This step ticks itself once the NVR asks the hub for events.":
    "No NVR, adicione a central como câmara ONVIF num canal livre: o seu endereço, porta 8899, utilizador admin e a palavra-passe da central. Depois ligue a deteção de movimento com gravação nesse canal. Este passo fica marcado sozinho quando o NVR pedir eventos à central.",
  "At least one sensor has reported": "Pelo menos um sensor já comunicou",
  "Pair a node": "Emparelhar um nó",
  "Allow pairing, then start a sensor node in pairing mode and approve it once its fingerprint matches the one on its label. A device that calls a web address can be added there too.":
    "Permita o emparelhamento, ligue um nó sensor em modo de emparelhamento e aprove-o quando a impressão digital for igual à da etiqueta. Também se pode adicionar lá um aparelho que chama um endereço web.",
  "A test alert sent, and its recording found on the NVR":
    "Um alerta de teste enviado, e a sua gravação encontrada no NVR",
  "A test alert in the last month (the last was {ago})": "Um alerta de teste no último mês (o último foi {ago})",
  "Sends a test alert along the whole path and checks each step: the sensor, the hub, the NVR and your notifications. Then find its recording on the NVR, so you know an alert is kept. Do it once a month.":
    "Envia um alerta de teste por todo o caminho e verifica cada passo: o sensor, a central, o NVR e as suas notificações. Depois procure a gravação no NVR, para saber que um alerta fica guardado. Faça-o uma vez por mês.",
  "Notifications or Home Assistant set up, so problems reach you":
    "Notificações ou Home Assistant configurados, para os problemas lhe chegarem",
  "Set up notifications": "Configurar notificações",
  "So an alert, a silent node or a flat battery reaches your phone: an ntfy or Gotify server on your network, Home Assistant, or Node-RED.":
    "Para um alerta, um nó em silêncio ou uma bateria vazia lhe chegarem ao telemóvel: um servidor ntfy ou Gotify na sua rede, o Home Assistant ou o Node-RED.",
  "A daily check, so you notice when the hub goes quiet":
    "Uma verificação diária, para dar conta quando a central se cala",
  "Choose a time": "Escolher uma hora",
  "Once a day the hub sends one short message. If it stops arriving, the hub is off or cut off, and you know to look.":
    "Uma vez por dia a central envia uma mensagem curta. Se deixar de chegar, a central está desligada ou isolada, e sabe que tem de ir ver.",
  "the NVR is not listening, so alerts are not recorded":
    "o NVR não está a ouvir, por isso os alertas não são gravados",
  "{n} sensor problem": "{n} problema num sensor",
  "{n} sensor problems": "{n} problemas nos sensores",
  "{n} setup step left": "falta {n} passo de configuração",
  "{n} setup steps left": "faltam {n} passos de configuração",
  "Not protecting: {why}": "Não está a proteger: {why}",
  "Needs attention: {why}": "Precisa de atenção: {why}",
  "All good: armed home, the NVR is listening, no problems.":
    "Tudo bem: armada em casa, o NVR está a ouvir, sem problemas.",
  "All good: armed away, the NVR is listening, no problems.":
    "Tudo bem: armada ausente, o NVR está a ouvir, sem problemas.",
  "just now": "agora mesmo",
  strong: "forte",
  good: "bom",
  fair: "razoável",
  weak: "fraco",
  "about {n} months left": "cerca de {n} meses restantes",
  "about {n} day left": "cerca de {n} dia restante",
  "about {n} days left": "cerca de {n} dias restantes",
  Never: "Nunca",
  "{n} s ago": "há {n} s",
  "{n} min ago": "há {n} min",
  "{n} h ago": "há {n} h",
  "{n} d ago": "há {n} d",
  "The changes on this page are not saved.": "As alterações nesta página não estão guardadas.",
  "Leave without saving?": "Sair sem guardar?",
  Leave: "Sair",
  Stay: "Ficar",
  "{name}: alarm hub": "{name}: central de alarme",
  "This browser will ask for the hub password again.": "Este navegador vai pedir outra vez a palavra-passe da central.",
  "Log out?": "Sair?",
  "Logged out": "Sessão terminada",
  "The hub's pages ask for the user admin and the hub password again.":
    "As páginas da central pedem outra vez o utilizador admin e a palavra-passe da central.",
  "Log in again": "Entrar outra vez",
  "The two passwords differ.": "As duas palavras-passe são diferentes.",
  "Saved. Log in with user admin and the new password.":
    "Guardada. Entre com o utilizador admin e a nova palavra-passe.",
  // sensors.js
  " (changed)": " (alteradas)",
  " (firmware too old)": " (firmware demasiado antigo)",
  " (waiting for the node)": " (à espera do nó)",
  "Add sensor": "Adicionar sensor",
  "Added node {mac}": "Nó {mac} adicionado",
  "Added sensor {n}": "Sensor {n} adicionado",
  "Alert settings": "Definições de alerta",
  "All clear": "Tudo calmo",
  "An alert lasts {hold} seconds. Empty uses the hub's default.":
    "Um alerta dura {hold} segundos. Vazio usa o valor da central.",
  'An alert this node reports as having happened more than {late} seconds ago is a past event: it shows the sensor\'s past-event picture, or the built-in "PAST EVENT, CHECK LOGS", and gets its own NVR record. Its true time is in the Events list. Empty uses 30 seconds. Set it to how long this node can take to deliver an alert in real time, including retries.':
    'Um alerta que este nó diga ter acontecido há mais de {late} segundos é um evento passado: mostra a imagem de evento passado do sensor, ou a incorporada "PAST EVENT, CHECK LOGS", e tem o seu próprio registo no NVR. A sua hora verdadeira está na lista de Eventos. Vazio usa 30 segundos. Defina-o como o tempo que este nó pode levar a entregar um alerta em tempo real, com as repetições.',
  "Battery's pin": "Pino da bateria",
  "Built-in": "Incorporada",
  "Built-in “ALL CLEAR”": "Incorporada “ALL CLEAR”",
  "Camera to look at: {camera}. The NVR camera that covers this sensor, named in its notifications and on the Events page, so you know which recording to watch.":
    "Câmara a ver: {camera}. A câmara do NVR que cobre este sensor, indicada nas suas notificações e na página de Eventos, para saber que gravação ver.",
  Cancel: "Cancelar",
  Change: "Alterar",
  "Change the all clear picture": "Alterar a imagem de tudo calmo",
  "Change the picture for {what}": "Alterar a imagem de {what}",
  "Change the words": "Alterar as palavras",
  Close: "Fechar",
  "Coming closer": "A aproximar-se",
  "Could not reach the hub.": "Não foi possível chegar à central.",
  "Could not show this picture. {why}": "Não foi possível mostrar esta imagem. {why}",
  Delete: "Apagar",
  "Delete it": "Apagá-la",
  "Delete node": "Apagar nó",
  "Delete sensor": "Apagar sensor",
  "Delete {what}?": "Apagar {what}?",
  "Deleted {what}": "Apagado: {what}",
  "Download it": "Descarregá-la",
  "Empty uses 180 minutes.": "Vazio usa 180 minutos.",
  "Enter a MAC address like AA:BB:CC:DD:EE:01": "Indique um endereço MAC como AA:BB:CC:DD:EE:01",
  "Enter a name for the first line": "Indique um nome para a primeira linha",
  "Enter the text to show": "Indique o texto a mostrar",
  "Getting the decoder ready. The first time, this downloads about 30 MB.":
    "A preparar o descodificador. Da primeira vez, isto descarrega cerca de 30 MB.",
  "Going away": "A afastar-se",
  "HC-SR501 passive infrared": "HC-SR501 infravermelho passivo",
  Hardware: "Hardware",
  "It goes to the NVR, Notifications and Home Assistant, whatever the alarm is set to, and is logged as a test.":
    "Vai para o NVR, as Notificações e o Home Assistant, seja qual for o estado do alarme, e fica registado como teste.",
  "Its pictures and settings go too. Its past alerts stay in the event history.":
    "As suas imagens e definições também vão. Os seus alertas passados ficam no histórico de eventos.",
  "Its sensors, their pictures and settings go too. Their past alerts stay in the event history.":
    "Os seus sensores, as imagens e as definições deles também vão. Os alertas passados ficam no histórico de eventos.",
  "LD2410 24 GHz radar": "LD2410 radar de 24 GHz",
  "Left to right": "Da esquerda para a direita",
  "Lithium iron phosphate (LiFePO4) cell": "Célula de fosfato de ferro-lítio (LiFePO4)",
  "Lithium-ion or lithium-polymer cell": "Célula de iões de lítio ou de polímero de lítio",
  "Live alert": "Alerta em direto",
  "Live alert: {state}": "Alerta em direto: {state}",
  "Past event: {state}": "Evento passado: {state}",
  "Loading the picture...": "A carregar a imagem...",
  "Make and save": "Criar e guardar",
  "Make and save 10 pictures": "Criar e guardar 10 imagens",
  "Make it from an image": "Criá-la a partir de uma imagem",
  "Making thumbnail {n} of {total}. The first time, this downloads about 30 MB.":
    "A criar a miniatura {n} de {total}. Da primeira vez, isto descarrega cerca de 30 MB.",
  "Name of sensor {n}": "Nome do sensor {n}",
  "Name, such as Back door": "Nome, como Porta das traseiras",
  "No nodes yet": "Ainda não há nós",
  "Node settings": "Definições do nó",
  "Node {mac}": "Nó {mac}",
  "Node {mac} already exists": "O nó {mac} já existe",
  OK: "OK",
  "Other, its output goes high while it detects": "Outro, a saída fica alta enquanto deteta",
  "Other, its output goes low while it detects": "Outro, a saída fica baixa enquanto deteta",
  "Pair a node, which adds its sensors for you, or add one by its MAC address above.":
    "Emparelhe um nó, que adiciona os seus sensores automaticamente, ou adicione um pelo endereço MAC acima.",
  "Past event": "Evento passado",
  "Picture storage: {used} KB of {total} KB used. {kept} KB of what is left is kept for the event history, so pictures cannot use it.":
    "Armazenamento de imagens: {used} KB de {total} KB usados. {kept} KB do que resta ficam para o histórico de eventos, por isso as imagens não os podem usar.",
  "Quiet hours from {from} to {to}. During quiet hours alerts are logged but not sent to the NVR or to Notifications. Leave both empty for none.":
    "Horas de silêncio das {from} às {to}. Durante as horas de silêncio os alertas ficam registados mas não são enviados ao NVR nem às Notificações. Deixe ambos vazios para nenhumas.",
  "RCWL-0516 microwave radar": "RCWL-0516 radar de micro-ondas",
  "Report this node silent after {silent} minutes without a message. A disabled or stolen sensor is noticed only then, so keep it short: a little over the time between the node's regular reports.":
    "Dar este nó como em silêncio após {silent} minutos sem mensagens. Um sensor desativado ou roubado só é notado nessa altura, por isso mantenha-o curto: um pouco mais do que o tempo entre as comunicações regulares do nó.",
  "Right to left": "Da direita para a esquerda",
  "Running, {n} min left.": "A decorrer, faltam {n} min.",
  "Same as Seen": "Igual à de Visto",
  "Save alert settings": "Guardar definições de alerta",
  "Save hardware": "Guardar hardware",
  "Save name": "Guardar nome",
  "Save node settings": "Guardar definições do nó",
  "Saved the alert settings": "Definições de alerta guardadas",
  "Saved the hardware; the node takes it after its next report":
    "Hardware guardado; o nó aplica-o depois da próxima comunicação",
  "Saved the name": "Nome guardado",
  "Saved the node settings": "Definições do nó guardadas",
  "Saved {file}": "Guardado: {file}",
  "Saved; the node takes them after its next report.": "Guardadas; o nó aplica-as depois da próxima comunicação.",
  Seen: "Visto",
  "Send a test alert from this sensor?": "Enviar um alerta de teste deste sensor?",
  "Send test alert": "Enviar alerta de teste",
  "Sensor number": "Número do sensor",
  "Sensor number must be 1 to 255": "O número do sensor tem de ser de 1 a 255",
  "Sensor {n}": "Sensor {n}",
  "Sensor {n} already exists on {mac}": "O sensor {n} já existe em {mac}",
  "Sensor's pin": "Pino do sensor",
  "Sent a test alert. Check that the NVR recorded it.": "Alerta de teste enviado. Confirme que o NVR o gravou.",
  "Set on the node itself; save here to set it from the hub.":
    "Definido no próprio nó; guarde aqui para o definir a partir da central.",
  "Show it": "Mostrá-la",
  "Shown for a live alert": "Mostrada num alerta em direto",
  "Shown for a past event": "Mostrada num evento passado",
  "Shown when no alert is on": "Mostrada quando não há alertas",
  "Start a walk test": "Iniciar um teste de passagem",
  "Tamper switch's pin": "Pino do interruptor antissabotagem",
  "The Seen picture, or the built-in one, is shown in its place.":
    "Em seu lugar é mostrada a imagem de Visto, ou a incorporada.",
  "The battery": "A bateria",
  "The battery's pin": "O pino da bateria",
  "The built-in one is shown in its place.": "Em seu lugar é mostrada a incorporada.",
  "The built-in picture is shown in its place.": "Em seu lugar é mostrada a imagem incorporada.",
  "The decoder could not load or could not read this picture. It needs internet access the first time.":
    "O descodificador não carregou ou não conseguiu ler esta imagem. Da primeira vez precisa de acesso à internet.",
  "The hub could not send this picture.": "A central não conseguiu enviar esta imagem.",
  "The hub could not send {file}": "A central não conseguiu enviar {file}",
  "The node reports that it is alive every {every} minutes, and every {repeat} seconds while its sensor stays on. Empty leaves each to the node's own setting. These reach the node with the hub's next answer to it, so after its next report. Less often saves its battery, but a stolen node is noticed later. The repeat is kept below each sensor's alert time, so the alert does not lapse between repeats.":
    "O nó comunica que está vivo a cada {every} minutos, e a cada {repeat} segundos enquanto o sensor continua ativo. Vazio deixa cada um à definição do próprio nó. Estes valores chegam ao nó com a próxima resposta da central, ou seja, depois da próxima comunicação. Menos vezes poupa a bateria, mas um nó roubado é notado mais tarde. A repetição fica abaixo do tempo de alerta de cada sensor, para o alerta não caducar entre repetições.",
  "The node runs newer settings than these.": "O nó tem definições mais recentes do que estas.",
  "The node runs these settings.": "O nó tem estas definições.",
  "The node says it reports every {every} minutes, so empty uses {silent} minutes, and follows the node if that changes.":
    "O nó diz que comunica a cada {every} minutos, por isso vazio usa {silent} minutos, e acompanha o nó se isso mudar.",
  "The pins listed are those this node's chip can use: ones that can wake it for the sensor and the tamper switch, ones that can read a voltage for the battery, through two equal resistors. The node checks them again before it takes them.":
    "Os pinos indicados são os que o chip deste nó pode usar: os que o podem acordar, para o sensor e o interruptor antissabotagem, e os que podem ler uma tensão, para a bateria, através de duas resistências iguais. O nó volta a verificá-los antes de os aplicar.",
  "The sensor's pin": "O pino do sensor",
  "The tamper switch's pin": "O pino do interruptor antissabotagem",
  "The walk test is on for 10 minutes": "O teste de passagem está ativo durante 10 minutos",
  "This cannot be undone.": "Isto não se pode desfazer.",
  "This node's firmware is from before hardware settings: update it, and it can take them from here.":
    "O firmware deste nó é anterior às definições de hardware: atualize-o, e passa a recebê-las daqui.",
  "Thumbnails are up to date": "As miniaturas estão em dia",
  Undo: "Desfazer",
  "Upload a .h264 file": "Enviar um ficheiro .h264",
  "Upload another .h264 file": "Enviar outro ficheiro .h264",
  "Walk test: for the next 10 minutes, the node blinks its LED after each alert the hub confirms, 3 times for a strong signal, 2 for a fair one and 1 for a weak one, so you can walk the area and see what it covers. It starts with the first alert the node sends after this.":
    "Teste de passagem: durante os próximos 10 minutos, o nó pisca o LED depois de cada alerta que a central confirmar, 3 vezes para um sinal forte, 2 para um razoável e 1 para um fraco, para poder percorrer a zona e ver o que cobre. Começa com o primeiro alerta que o nó enviar depois disto.",
  "YOUR PICTURE": "A SUA IMAGEM",
  "Your picture": "A sua imagem",
  "Your picture, {size} KB": "A sua imagem, {size} KB",
  "Zone: {zone}. Mark the sensors inside the house as interior, so the hub can be armed home at night.":
    "Zona: {zone}. Marque os sensores dentro de casa como interiores, para a central poder ficar armada em casa à noite.",
  default: "predefinido",
  "interior: not watched while the hub is armed home": "interior: não vigiado com a central armada em casa",
  "perimeter: watched whenever the hub is armed": "perímetro: vigiado sempre que a central está armada",
  "sensor no.": "n.º do sensor",
  "such as Camera 3, garden": "como Câmara 3, jardim",
  "the node {name}": "o nó {name}",
  "the sensor {name}": "o sensor {name}",
  "this picture": "esta imagem",
  "{file} is larger than 32 KB": "{file} tem mais de 32 KB",
  // assist.js
  Copy: "Copiar",
  Copied: "Copiado",
  "Select and copy it": "Selecione e copie",
  "Add a sensor first: there is nothing to test with.": "Adicione primeiro um sensor: não há nada com que testar.",
  "No sensor yet": "Ainda não há sensores",
  Start: "Iniciar",
  "Sends a real test alert, from the sensor you choose, along the whole path: the NVR, Notifications and Home Assistant. It records on the NVR like a real alert, and is logged as a test.":
    "Envia um alerta de teste real, do sensor que escolher, por todo o caminho: o NVR, as Notificações e o Home Assistant. Grava no NVR como um alerta real, e fica registado como teste.",
  "Test with {sensor} {start}": "Testar com {sensor} {start}",
  "Test alert from {name}": "Alerta de teste de {name}",
  "The NVR was told": "O NVR foi avisado",
  "no NVR address is set, so the hub cannot tell (Recording > NVR)":
    "não há endereço do NVR, por isso a central não consegue saber (Gravação > NVR)",
  "the NVR is not listening; the hub keeps the alert and plays it once it does":
    "o NVR não está a ouvir; a central guarda o alerta e repete-o quando ele ouvir",
  "not set up": "não configurado",
  "no answer": "sem resposta",
  "connected, so it got the test": "ligado, por isso recebeu o teste",
  "Now check the NVR: it should have a recording from the hub's channel at {time}, with {name}'s picture.":
    "Agora veja no NVR: deve haver uma gravação do canal da central às {time}, com a imagem de {name}.",
  "What kind of sensor is it?": "Que tipo de sensor é?",
  "A sensor node": "Um nó sensor",
  "one of these nodes, which talks to the hub over its own radio":
    "um destes nós, que fala com a central pelo seu próprio rádio",
  "A device that calls a web address": "Um aparelho que chama um endereço web",
  "such as a Shelly, a Tasmota plug or a script": "como um Shelly, uma tomada Tasmota ou um script",
  "Pairing is open for 2 minutes. Power the node on, or reset it while holding its forget button. It appears on this page under Pairing with its fingerprint: check that it matches the node's, approve it, and say where each of its sensors is.":
    "O emparelhamento está aberto durante 2 minutos. Ligue o nó, ou reinicie-o com o botão de esquecer premido. Aparece nesta página em Emparelhamento com a sua impressão digital: confirme que é igual à do nó, aprove-o, e diga onde está cada um dos seus sensores.",
  "Back door": "Porta das traseiras",
  "Add it": "Adicionar",
  "Where is it": "Onde está",
  "Its MAC address": "O seu endereço MAC",
  "The device's own, if it shows one, or the made-up one here. Input 1 unless it has several.":
    "O do próprio aparelho, se mostrar um, ou o inventado aqui. Entrada 1, a não ser que tenha várias.",
  Input: "Entrada",
  "Waiting for its first report...": "À espera da primeira comunicação...",
  "That sensor already existed, so it was kept as it was, with the new name.":
    "Esse sensor já existia, por isso ficou como estava, com o novo nome.",
  "That sensor already existed, so it was kept as it was.": "Esse sensor já existia, por isso ficou como estava.",
  "Set the device to call this address when it detects something:":
    "Configure o aparelho para chamar este endereço quando detetar algo:",
  "and, if it can, this one when it stops (optional: the alert ends by itself):":
    "e, se puder, este quando parar (opcional: o alerta acaba sozinho):",
  "A report arrived. It is ready; try a test alert from its page.":
    "Chegou uma comunicação. Está pronto; experimente um alerta de teste na sua página.",
  "Its first report arrived. It is ready; try a test alert from its page.":
    "Chegou a primeira comunicação. Está pronto; experimente um alerta de teste na sua página.",
  "Open its page": "Abrir a sua página",
  // events.js, and the hub's event names and where an arm came from
  "The sensor saw something, and the NVR was told, so it records.":
    "O sensor viu algo, e o NVR foi avisado, por isso grava.",
  "Check the recording if nobody was expected.": "Veja a gravação se não se esperava ninguém.",
  "No report from this sensor for longer than its silent time. Its battery may be flat, or it was unplugged, moved or taken.":
    "Nenhuma comunicação deste sensor há mais tempo do que o seu tempo de silêncio. A bateria pode estar vazia, ou foi desligado, mudado de sítio ou levado.",
  "Check the sensor and its battery. This clears by itself once it reports again.":
    "Verifique o sensor e a bateria. Isto desaparece sozinho quando voltar a comunicar.",
  "Its battery is at 20% or less.": "A bateria está a 20% ou menos.",
  "Replace or charge it soon.": "Substitua-a ou carregue-a em breve.",
  "The sensor fired during its quiet hours, so the NVR was not told.":
    "O sensor disparou durante as horas de silêncio, por isso o NVR não foi avisado.",
  "If this should have been recorded, change the quiet hours in the sensor's Alert settings.":
    "Se isto devia ter sido gravado, altere as horas de silêncio nas Definições de alerta do sensor.",
  "Its radio signal has stayed below -80 dBm, so its messages may be lost.":
    "O sinal de rádio tem ficado abaixo de -80 dBm, por isso as mensagens podem perder-se.",
  "Move the node or the hub closer, or raise the node off the ground and away from metal.":
    "Aproxime o nó da central, ou levante o nó do chão e afaste-o de metal.",
  "The NVR was not listening when this alert came, so it did not record it then. The hub keeps it and plays it as a past event once the NVR listens again.":
    "O NVR não estava a ouvir quando este alerta chegou, por isso não o gravou nessa altura. A central guarda-o e repete-o como evento passado quando o NVR voltar a ouvir.",
  "Check that the NVR is on and on the network.": "Confirme que o NVR está ligado e na rede.",
  "The NVR has not been listening for 3 minutes, so alerts are not being recorded.":
    "O NVR não está a ouvir há 3 minutos, por isso os alertas não estão a ser gravados.",
  "Check that the NVR is on, on the network, and still has the hub's channel set up.":
    "Confirme que o NVR está ligado, na rede, e ainda tem o canal da central configurado.",
  "The NVR is listening again. Alerts kept for it play now.":
    "O NVR está a ouvir outra vez. Os alertas guardados para ele são repetidos agora.",
  "The hub started, for the reason shown.": "A central arrancou, pelo motivo indicado.",
  "If power cuts are common, a small UPS keeps it running.":
    "Se os cortes de energia forem frequentes, uma pequena UPS mantém-na a funcionar.",
  "The hub was off its Wi-Fi for the time shown. Alerts in that time reached the NVR afterwards, as past events.":
    "A central esteve fora do Wi-Fi durante o tempo indicado. Os alertas desse período chegaram depois ao NVR, como eventos passados.",
  "If this happens often, check the Wi-Fi where the hub is. Someone may be keeping it off on purpose.":
    "Se isto acontecer muitas vezes, verifique o Wi-Fi onde está a central. Alguém pode estar a afastá-la de propósito.",
  "The sensor fired while the hub was disarmed, so the NVR was not told.":
    "O sensor disparou com a central desarmada, por isso o NVR não foi avisado.",
  "Arm the hub whenever nobody is home.": "Arme a central sempre que não estiver ninguém em casa.",
  "The hub was armed, from where the details say.": "A central foi armada, a partir de onde dizem os detalhes.",
  "The hub was disarmed, from where the details say. Alerts are logged but not recorded.":
    "A central foi desarmada, a partir de onde dizem os detalhes. Os alertas ficam registados mas não são gravados.",
  "An interior sensor fired while the hub was armed home, so the NVR was not told.":
    "Um sensor interior disparou com a central armada em casa, por isso o NVR não foi avisado.",
  "If it should always record, mark it perimeter in its Alert settings.":
    "Se deve gravar sempre, marque-o como perímetro nas Definições de alerta.",
  "The hub was armed home: interior sensors are logged but not recorded.":
    "A central foi armada em casa: os sensores interiores ficam registados mas não são gravados.",
  "A test alert, sent from the page or Home Assistant. It records like a real one.":
    "Um alerta de teste, enviado da página ou do Home Assistant. Grava como um real.",
  "Check that the NVR has the recording.": "Confirme que o NVR tem a gravação.",
  "The hub restarted by itself three times within an hour: it is crashing, losing power or being restarted.":
    "A central reiniciou sozinha três vezes numa hora: está a falhar, a perder energia ou a ser reiniciada.",
  "Check its power supply. Device Info shows the reasons.":
    "Verifique a fonte de alimentação. A Informação do aparelho mostra os motivos.",
  "The node restarted because of a fault, such as a brownout from a weak battery, or a bad wire.":
    "O nó reiniciou por uma falha, como uma quebra de tensão por bateria fraca, ou um fio com mau contacto.",
  "Check its battery and its wiring.": "Verifique a bateria e as ligações.",
  "From its readings, the battery will run out within a week.":
    "Pelas leituras, a bateria vai acabar dentro de uma semana.",
  "Replace or charge it.": "Substitua-a ou carregue-a.",
  "Several nodes stopped reporting at the same time. Batteries fail one at a time, so something may be blocking the radio, such as a jammer.":
    "Vários nós deixaram de comunicar ao mesmo tempo. As baterias falham uma de cada vez, por isso algo pode estar a bloquear o rádio, como um bloqueador.",
  "Check the property now: this can mean a break-in.": "Verifique já a propriedade: isto pode significar uma intrusão.",
  "The sensor fired 20 times within an hour. Every alert still went to the NVR, but a fault or a bad aim is likelier than that many intruders.":
    "O sensor disparou 20 vezes numa hora. Todos os alertas foram para o NVR, mas uma avaria ou uma má orientação é mais provável do que tantos intrusos.",
  "Check what it sees, such as moving plants, sun, pets or heating, and its wiring.":
    "Veja o que ele vê, como plantas a mexer, sol, animais ou aquecimento, e as ligações.",
  "The sensor reports nothing in front of it any more. Its alert ends when the alert time runs out.":
    "O sensor já não comunica nada à sua frente. O alerta acaba quando acabar o tempo de alerta.",
  Signal: "Sinal",
  "Hub heard it in": "A central ouviu-o em",
  "fired {n} times": "disparou {n} vezes",
  fired: "disparou",
  waiting: "à espera",
  "No sensor nodes yet. Pair one under Sensors > Nodes and pairing.":
    "Ainda não há nós sensores. Emparelhe um em Sensores > Nós e emparelhamento.",
  "On for {n} node for 10 minutes. Walk past each sensor; each node starts on its next report.":
    "Ativo em {n} nó durante 10 minutos. Passe por cada sensor; cada nó começa na sua próxima comunicação.",
  "On for {n} nodes for 10 minutes. Walk past each sensor; each node starts on its next report.":
    "Ativo em {n} nós durante 10 minutos. Passe por cada sensor; cada nó começa na sua próxima comunicação.",
  "Stopped. What fired stays listed until you leave this page.":
    "Parado. O que disparou fica na lista até sair desta página.",
  approaching: "a aproximar-se",
  "moving away": "a afastar-se",
  "left to right": "da esquerda para a direita",
  "right to left": "da direita para a esquerda",
  "{m} min": "{m} min",
  "{h} h {m} min": "{h} h {m} min",
  "{d} days {h} h": "{d} dias {h} h",
  "the hub": "a central",
  Event: "Evento",
  Details: "Detalhes",
  "off for up to {time}": "desligada até {time}",
  "look at {camera}": "ver {camera}",
  "Tap for what this means": "Toque para ver o que significa",
  "Clock not set": "Relógio por acertar",
  Hub: "Central",
  "Show {n} more": "Mostrar mais {n}",
  "None of these": "Nenhum destes",
  "Nothing of this kind among the latest 64 events. The CSV has the whole history.":
    "Nada deste tipo nos últimos 64 eventos. O CSV tem o histórico completo.",
  "No events yet": "Ainda não há eventos",
  "Alerts, problems and the hub's own events show here as they happen.":
    "Os alertas, os problemas e os eventos da própria central aparecem aqui à medida que acontecem.",
  "Send a test alert from the Overview": "Enviar um alerta de teste no Resumo",
  clear: "fim",
  "delayed alert": "alerta atrasado",
  alert: "alerta",
  "the web page": "a página web",
  "the re-arm timer": "o temporizador de rearme",
  "{from}, for {n} min": "{from}, por {n} min",
  "{from}, for {n} h": "{from}, por {n} h",
  "{from}, for {n} day": "{from}, por {n} dia",
  "{from}, for {n} days": "{from}, por {n} dias",
  // guide.js, node.js, plan.js
  " (low)": " (fraca)",
  " (not on the plan yet)": " (ainda não está na planta)",
  "(it takes {set} after its next report)": "(passa a {set} depois da próxima comunicação)",
  ", saving it": ", a poupá-la",
  ", weak on average": ", fraco em média",
  "A new node": "Um nó novo",
  "A problem: silent, low battery, weak signal or refused messages":
    "Um problema: em silêncio, bateria fraca, sinal fraco ou mensagens recusadas",
  "A test alert reached the NVR": "Um alerta de teste chegou ao NVR",
  "Allow pairing for 2 minutes": "Permitir emparelhamento durante 2 minutos",
  "Allow pairing, then reset the node, or power it up for the first time. It flashes yellow while it looks for the hub.":
    "Permita o emparelhamento e depois reinicie o nó, ou ligue-o pela primeira vez. Pisca a amarelo enquanto procura a central.",
  "Approve it": "Aprove-o",
  Board: "Placa",
  "Check it again": "Verificar outra vez",
  Checked: "Verificado",
  "Choose a sensor above first.": "Escolha primeiro um sensor acima.",
  "Choose a sensor to place": "Escolha um sensor para colocar",
  "Choose its sensor and pins": "Escolha o sensor e os pinos",
  "Choose its sensors on the Floor plan page and tap where they are.":
    "Escolha os seus sensores na página Planta e toque onde estão.",
  "Delete the floor plan?": "Apagar a planta?",
  "Deleted.": "Apagada.",
  Done: "Feito",
  "Every sensor is on the plan. Tap a spot to open that sensor's page.":
    "Todos os sensores estão na planta. Toque num ponto para abrir a página desse sensor.",
  "Every sensor's place on it goes too.": "O lugar de cada sensor nela também vai.",
  "Fair: may miss a message now and then": "Razoável: pode perder uma mensagem de vez em quando",
  "Firing now": "A disparar agora",
  Firmware: "Firmware",
  "Flash it": "Grave-o",
  Good: "Bom",
  "Heard at a good signal": "Ouvido com bom sinal",
  "Its firmware is from before hardware settings": "O firmware é anterior às definições de hardware",
  "Its signal is {signal}, and weak on average. Move the node, or the hub, until it is good or strong: higher, nearer, with fewer walls between them. Each report updates this.":
    "O sinal é {signal}, e fraco em média. Mude o nó, ou a central, de sítio até ficar bom ou forte: mais alto, mais perto, com menos paredes entre eles. Cada comunicação atualiza isto.",
  "Its signal is {signal}. Move the node, or the hub, until it is good or strong: higher, nearer, with fewer walls between them. Each report updates this.":
    "O sinal é {signal}. Mude o nó, ou a central, de sítio até ficar bom ou forte: mais alto, mais perto, com menos paredes entre eles. Cada comunicação atualiza isto.",
  "Its signal was good, the NVR got its test alert, and it saw someone walk past, by {date}.":
    "O sinal era bom, o NVR recebeu o alerta de teste, e viu alguém passar, até {date}.",
  "Its signal was good, the NVR got its test alert, and it saw someone walk past.":
    "O sinal era bom, o NVR recebeu o alerta de teste, e viu alguém passar.",
  "Last heard": "Ouvido pela última vez",
  "Let it pair": "Deixe-o emparelhar",
  "Making it small enough for the hub...": "A reduzi-la para caber na central...",
  "Mount the node, press its reset button, and walk past it from where an intruder would come: its light flashes 3, 2 or 1 times for a strong, fair or weak signal, and its alert shows here.":
    "Monte o nó, carregue no botão de reinício, e passe por ele vindo de onde viria um intruso: a luz pisca 3, 2 ou 1 vezes para um sinal forte, razoável ou fraco, e o alerta aparece aqui.",
  "Name its sensors": "Dê nome aos sensores",
  "No floor plan yet. Add one under Alarm > Floor plan.": "Ainda não há planta. Adicione uma em Alarme > Planta.",
  "No such node": "Esse nó não existe",
  "No: it uses the shared ESP-NOW key": "Não: usa a chave ESP-NOW partilhada",
  "None among the latest 64 events.": "Nenhum nos últimos 64 eventos.",
  "Not heard yet": "Ainda não ouvido",
  "Not heard yet: press its reset button, and it reports at once.":
    "Ainda não ouvido: carregue no botão de reinício, e comunica logo.",
  "Not known": "Desconhecida",
  "Not known yet": "Ainda desconhecido",
  "Now tap where {sensor} is on the plan.": "Agora toque onde está {sensor} na planta.",
  "On the Sensors page, name each of its sensors by where it is, such as Back door: alerts and notifications use the name.":
    "Na página Sensores, dê a cada sensor o nome do sítio onde está, como Porta das traseiras: os alertas e as notificações usam esse nome.",
  "On the Sensors page, under the node's Hardware, choose its sensor and the pins you wired.":
    "Na página Sensores, no Hardware do nó, escolha o sensor e os pinos que ligou.",
  "Once the node says hello, it is listed under Pairing below with its fingerprint.":
    "Quando o nó se apresentar, aparece em Emparelhamento, abaixo, com a sua impressão digital.",
  "Open the Sensors page": "Abrir a página Sensores",
  "Open the floor plan": "Abrir a planta",
  Paired: "Emparelhado",
  "Placed.": "Colocado.",
  "Put it on the floor plan": "Coloque-o na planta",
  Quiet: "Calmo",
  Reports: "Comunicações",
  "Saved ({size} KB). Now place each sensor on it.": "Guardada ({size} KB). Agora coloque nela cada sensor.",
  "Saved: the node takes them after its next report and restarts into them.":
    "Guardado: o nó aplica-os depois da próxima comunicação e reinicia com eles.",
  "Send it a test alert: the NVR should record, as it would for an intruder.":
    "Envie-lhe um alerta de teste: o NVR deve gravar, como faria com um intruso.",
  "Sent. Once the alert is over, the hub sees whether the NVR got it.":
    "Enviado. Quando o alerta acabar, a central vê se o NVR o recebeu.",
  "Start a walk test on it": "Iniciar um teste de passagem nele",
  Strong: "Forte",
  "Taken off the plan.": "Tirado da planta.",
  "That picture is too detailed to fit the hub's 32 KB. Try a simpler drawing, or crop it.":
    "Essa imagem tem demasiado detalhe para caber nos 32 KB da central. Experimente um desenho mais simples, ou recorte-a.",
  "The NVR did not get its test alert. See on the Overview that the NVR is listening, then send another.":
    "O NVR não recebeu o alerta de teste. Veja no Resumo se o NVR está a ouvir, e depois envie outro.",
  "The hub has it: compare the fingerprint below with the one on the node's label or screen, and approve it under Pairing if they match. It then flashes green three times.":
    "A central já o tem: compare a impressão digital abaixo com a da etiqueta ou do ecrã do nó, e aprove-o em Emparelhamento se forem iguais. Depois pisca a verde três vezes.",
  "This node's firmware is from before hardware settings: update it first, and it can take them from the hub.":
    "O firmware deste nó é anterior às definições de hardware: atualize-o primeiro, e passa a recebê-las da central.",
  "Three checks before you trust it, each ticked as the hub sees it done.":
    "Três verificações antes de confiar nele, cada uma marcada quando a central a vê feita.",
  "To do": "Por fazer",
  "Walk past it": "Passe por ele",
  "Walk past it from where an intruder would come, and each of its sensors must see you. During a walk test its light flashes 3, 2 or 1 times for a strong, fair or weak signal.":
    "Passe por ele vindo de onde viria um intruso, e cada um dos seus sensores tem de o ver. Durante um teste de passagem a luz pisca 3, 2 ou 1 vezes para um sinal forte, razoável ou fraco.",
  "Weak: move the node or the hub": "Fraco: mude o nó ou a central de sítio",
  "Write the node's firmware to it over USB: run python tools/flasher.py in the node project and choose its board, which also prints its label with its fingerprint. Or build it with PlatformIO, as the node's README says.":
    "Grave o firmware no nó por USB: execute python tools/flasher.py no projeto do nó e escolha a placa, o que também imprime a etiqueta com a impressão digital. Ou compile-o com o PlatformIO, como diz o README do nó.",
  Yes: "Sim",
  "Yes, its messages sealed": "Sim, com as mensagens seladas",
  "every {n} minutes": "a cada {n} minutos",
  "firing now": "a disparar agora",
  "generic, active high": "genérico, ativo em alto",
  "generic, active low": "genérico, ativo em baixo",
  "lithium-ion": "iões de lítio",
  "not heard yet": "ainda não ouvido",
  quiet: "calmo",
  "sensor {n}": "sensor {n}",
  silent: "em silêncio",
  unknown: "desconhecido",
  "{cell} on {pin}": "{cell} em {pin}",
  "{node}: set up": "{node}: configurado",
  "{n} refused": "{n} recusadas",
  "{n} restarts, the last: {why}": "{n} reinícios, o último: {why}",
  "{n} sensor is not on the plan yet: choose one above, then tap where it is.":
    "{n} sensor ainda não está na planta: escolha-o acima, e depois toque onde está.",
  "{n} sensors are not on the plan yet: choose one above, then tap where it is.":
    "{n} sensores ainda não estão na planta: escolha um acima, e depois toque onde está.",
  "{sensor} on {pin}; tamper switch: {tamper}; battery: {battery}":
    "{sensor} em {pin}; interruptor antissabotagem: {tamper}; bateria: {battery}",
  // overview.js
  "30 minutes": "30 minutos",
  "1 hour": "1 hora",
  "2 hours": "2 horas",
  "4 hours": "4 horas",
  "8 hours": "8 horas",
  "12 hours": "12 horas",
  "1 day": "1 dia",
  "until armed again": "até voltar a armar",
  "for {time}": "durante {time}",
  "{h} h": "{h} h",
  "Every sensor counts.": "Todos os sensores contam.",
  "Interior sensors are logged but not sent to the NVR.":
    "Os sensores interiores ficam registados mas não são enviados ao NVR.",
  "Until armed again.": "Até voltar a armar.",
  "Arms again in {time}.": "Volta a armar dentro de {time}.",
  "Arms again soon.": "Volta a armar em breve.",
  "Alerts are logged but not sent to the NVR, so nothing is recorded.":
    "Os alertas ficam registados mas não são enviados ao NVR, por isso nada é gravado.",
  "check its battery, or whether it was moved": "verifique a bateria, ou se foi mudado de sítio",
  "replace or charge it": "substitua-a ou carregue-a",
  "move it or the hub closer": "aproxime-o da central",
  "it may have the wrong key; pair it again": "pode ter a chave errada; emparelhe-o outra vez",
  "messages refused": "mensagens recusadas",
  "Before the disarm at {time}": "Antes do desarme às {time}",
  "Since it was last armed": "Desde que foi armada pela última vez",
  "While armed, {from} to {to}": "Enquanto armada, das {from} às {to}",
  "Since armed at {time}": "Desde que foi armada às {time}",
  "at least {n} alert": "pelo menos {n} alerta",
  "at least {n} alerts": "pelo menos {n} alertas",
  "{n} alert": "{n} alerta",
  "{n} alerts": "{n} alertas",
  " and more": " e mais",
  "{n} came while the NVR was not listening, and were played to it once it listened again":
    "{n} chegaram com o NVR sem ouvir, e foram-lhe repetidos quando voltou a ouvir",
  "{n} did not reach the NVR yet; the hub keeps them until it listens":
    "{n} ainda não chegaram ao NVR; a central guarda-os até ele ouvir",
  "all reached the NVR": "todos chegaram ao NVR",
  "with no NVR address set, the hub cannot tell whether they reached it":
    "sem endereço do NVR, a central não consegue saber se lhe chegaram",
  "no alerts.": "nenhum alerta.",
  "Not set up": "Não configurado",
  Connected: "Ligado",
  Connecting: "A ligar",
  " (a past event replaying)": " (um evento passado a ser repetido)",
  "Sensor problems": "Problemas dos sensores",
  "Last alert": "Último alerta",
  "None in the history": "Nenhum no histórico",
  "No address set": "Sem endereço",
  "Listening, watching the video": "A ouvir, a ver o vídeo",
  Listening: "A ouvir",
  "Not listening: alerts are not reaching it": "Não está a ouvir: os alertas não lhe chegam",
  "Last message: {result}": "Última mensagem: {result}",
  "Nothing sent yet": "Ainda nada enviado",
  Fix: "Corrigir",
  "Getting started: step {n} of {total}": "Primeiros passos: passo {n} de {total}",
  "Getting started": "Primeiros passos",
  "Back to {mode}": "De volta a {mode}",
  "Not changed: {why}": "Não mudou: {why}",
  "Disarm the hub {when}?": "Desarmar a central {when}?",
  "Disarmed {when}": "Desarmada {when}",
  Saved: "Guardado",
  "Not saved: {why}": "Não guardado: {why}",
  // nodes.js, and the hub's words for a node's update and refused messages
  "Hub MAC address": "Endereço MAC da central",
  "WiFi channel": "Canal Wi-Fi",
  "ESP-NOW key": "Chave ESP-NOW",
  Active: "Ativo",
  Idle: "Em repouso",
  Silent: "Em silêncio",
  State: "Estado",
  "Report no.": "N.º da comunicação",
  Refused: "Recusadas",
  Key: "Chave",
  "Sensor type": "Tipo de sensor",
  Restarts: "Reinícios",
  "Last alert took": "O último alerta levou",
  "(no name)": "(sem nome)",
  "(no report for {n} minutes)": "(sem comunicações há {n} minutos)",
  "its own": "própria",
  shared: "partilhada",
  ", sealed": ", selada",
  "{n}, last: {why}": "{n}, o último: {why}",
  " (a fault)": " (uma falha)",
  "No sensor nodes yet": "Ainda não há nós sensores",
  "Allow pairing and start a node in pairing mode, or add a device that calls a web address.":
    "Permita o emparelhamento e ligue um nó em modo de emparelhamento, ou adicione um aparelho que chama um endereço web.",
  "Allow pairing": "Permitir emparelhamento",
  "{on} of {all} nodes run it.": "{on} de {all} nós usam-no.",
  "Offered to every node of this chip.": "Oferecido a todos os nós deste chip.",
  "{name} had it undone, so it is not offered to the others.": "{name} desfê-lo, por isso não é oferecido aos outros.",
  "Tried on {name} first; the others get it a day after it runs it.":
    "Experimentado primeiro em {name}; os outros recebem-no um dia depois de ele o usar.",
  "{name} runs it; the others get it in {n} h.": "{name} usa-o; os outros recebem-no dentro de {n} h.",
  "{name} ran it for a day, so the others get it too.":
    "{name} usou-o durante um dia, por isso os outros também o recebem.",
  Chip: "Chip",
  Version: "Versão",
  Size: "Tamanho",
  Nodes: "Nós",
  "Try it first on": "Experimentar primeiro em",
  "None on the hub.": "Nenhum na central.",
  "The node to try it on first": "O nó onde o experimentar primeiro",
  "It is tried on {name} first": "É experimentado primeiro em {name}",
  "Offered to every node": "Oferecido a todos os nós",
  "No node: every node at once": "Nenhum: todos os nós de uma vez",
  Messages: "Mensagens",
  "These are ignored until added under Sensors > Sensors.":
    "Estes são ignorados até serem adicionados em Sensores > Sensores.",
  "None since the hub last started.": "Nenhum desde que a central arrancou.",
  "Not available on this hub.": "Não disponível nesta central.",
  "Open for {n} s more.": "Aberto durante mais {n} s.",
  "Closed.": "Fechado.",
  "The hub's fingerprint is {fingerprint}.": "A impressão digital da central é {fingerprint}.",
  Fingerprint: "Impressão digital",
  " (paired before, with another key)": " (emparelhado antes, com outra chave)",
  " (added before, without pairing)": " (adicionado antes, sem emparelhamento)",
  Approve: "Aprovar",
  "Only if its label shows the fingerprint {fingerprint}.":
    "Só se a etiqueta mostrar a impressão digital {fingerprint}.",
  "Approve {mac}?": "Aprovar {mac}?",
  "For example: Back door. Alerts and notifications will use this name.":
    "Por exemplo: Porta das traseiras. Os alertas e as notificações vão usar este nome.",
  "Where is sensor {n} of this node?": "Onde está o sensor {n} deste nó?",
  "Where is this sensor?": "Onde está este sensor?",
  Skip: "Saltar",
  "The name was not saved: {why}. Name it on the Sensors page.":
    "O nome não foi guardado: {why}. Dê-lhe nome na página Sensores.",
  "Paired {mac}. It starts reporting once it hears the hub again.":
    "{mac} emparelhado. Começa a comunicar quando voltar a ouvir a central.",
  Reject: "Rejeitar",
  "Node problems now raise the alarm": "Os problemas dos nós passam a dar alarme",
  "Node problems no longer raise the alarm": "Os problemas dos nós deixam de dar alarme",
  "Choose a node's firmware .bin file first.": "Escolha primeiro o ficheiro .bin do firmware de um nó.",
  "Uploading. Keep this page open.": "A enviar. Mantenha esta página aberta.",
  "Done. The nodes of that chip update after their next report.":
    "Feito. Os nós desse chip atualizam-se depois da próxima comunicação.",
  "The upload failed.": "O envio falhou.",
  "update ready": "atualização pronta",
  "update undone": "atualização desfeita",
  "updating, {n}%": "a atualizar, {n}%",
  "wrong signature": "assinatura errada",
  "Alerts will be logged but not sent to the NVR, so nothing is recorded.":
    "Os alertas vão ficar registados mas não serão enviados ao NVR, por isso nada será gravado.",
  // network.js
  "Video address": "Endereço do vídeo",
  "Video streams open": "Ligações de vídeo abertas",
  "Leave empty to keep it": "Deixe vazio para a manter",
  Login: "Acesso",
  "Required: user admin and the hub password": "Obrigatório: utilizador admin e a palavra-passe da central",
  "Device address": "Endereço do aparelho",
  "Event subscribers": "Inscritos nos eventos",
  "Serial number": "Número de série",
  "Requests not supported": "Pedidos não suportados",
  "Sensor token": "Token dos sensores",
  "add {token} to /sensor requests": "acrescente {token} aos pedidos /sensor",
  "Could not reach the hub. Trying again.": "Não foi possível chegar à central. A tentar outra vez.",
  "The hub is short of memory. Trying again.": "A central tem pouca memória. A tentar outra vez.",
  "The hub could not send the picture.": "A central não conseguiu enviar a imagem.",
  "The picture changed while it was being sent. Trying again.":
    "A imagem mudou enquanto era enviada. A tentar outra vez.",
  "Could not show the picture. The decoder needs internet access the first time.":
    "Não foi possível mostrar a imagem. Da primeira vez, o descodificador precisa de acesso à internet.",
  "Could not show the picture.": "Não foi possível mostrar a imagem.",
  "Enter the NVR's IP address": "Indique o endereço IP do NVR",
  "Not connected": "Desligado",
  Video: "Vídeo",
  "Motion events": "Eventos de movimento",
  "Viewers connected now": "Leitores ligados agora",
  "Saved.": "Guardado.",
  "This fixed address is used only on {network}. Other WiFi networks use DHCP.":
    "Este endereço fixo só é usado em {network}. As outras redes Wi-Fi usam DHCP.",
  "A fixed address is used only on the Wi-Fi network it is connected to now.":
    "Um endereço fixo só é usado na rede Wi-Fi a que está ligada agora.",
  "Restart the hub to use this address?": "Reiniciar a central para usar este endereço?",
  "Save and restart": "Guardar e reiniciar",
  "Saved. The hub is restarting.": "Guardado. A central está a reiniciar.",
  "Saved. The hub is restarting and will get its address from the router. Open it by name, or find the new address in the router.":
    "Guardado. A central está a reiniciar e vai receber o endereço do router. Abra-a pelo nome, ou procure o novo endereço no router.",
  "Restart the hub to use this port?": "Reiniciar a central para usar esta porta?",
  "The video stops briefly. A viewer such as VLC will need the new address.":
    "O vídeo para por instantes. Um leitor como o VLC vai precisar do novo endereço.",
  "The NVR will lose this camera until its channel is set to the new port.":
    "O NVR vai perder esta câmara até o canal ter a nova porta.",
  "A new port or name restarts the hub; a new password or other names apply at once.":
    "Uma nova porta ou nome reinicia a central; uma nova palavra-passe ou outros nomes aplicam-se de imediato.",
  "Save these settings?": "Guardar estas definições?",
  "Saved. A new password applies now: log in again with it.":
    "Guardado. Uma nova palavra-passe aplica-se já: entre outra vez com ela.",
  // notify.js, and the hub's words for its connections
  saved: "guardada",
  "since {time}": "desde {time}",
  "Events waiting to send": "Eventos à espera de envio",
  "Events lost, too many waiting": "Eventos perdidos, demasiados à espera",
  Topics: "Tópicos",
  "In Home Assistant": "No Home Assistant",
  "the device {hub}, and one device per sensor": "o aparelho {hub}, e um aparelho por sensor",
  "Saved. The connection shows below within a few seconds.":
    "Guardado. A ligação aparece abaixo dentro de alguns segundos.",
  "The address of your ntfy server, ending with the topic to send to. Subscribe to that topic in the ntfy app on your phone. Alerts arrive as urgent, problems as high priority, and the daily check and weekly summary quietly.":
    "O endereço do seu servidor ntfy, a terminar com o tópico para onde enviar. Inscreva-se nesse tópico na aplicação ntfy do telemóvel. Os alertas chegam como urgentes, os problemas com prioridade alta, e a verificação diária e o resumo semanal discretamente.",
  "Access token, if the topic needs one": "Token de acesso, se o tópico precisar",
  "The address of your Gotify server, ending with /message, and an application's token from its Apps page. Alerts arrive with the highest priority.":
    "O endereço do seu servidor Gotify, a terminar com /message, e o token de uma aplicação da página Apps. Os alertas chegam com a prioridade máxima.",
  "Application token": "Token da aplicação",
  "Each message is posted as JSON, with the event, the sensor, its name and camera, the battery and the time. In Home Assistant, use a webhook trigger in an automation.":
    "Cada mensagem é enviada como JSON, com o evento, o sensor, o seu nome e câmara, a bateria e a hora. No Home Assistant, use um acionador webhook numa automação.",
  "Each message is posted as one line of plain text.": "Cada mensagem é enviada como uma linha de texto simples.",
  "saved; leave blank to keep it": "guardado; deixe vazio para o manter",
  "Last message": "Última mensagem",
  "None sent yet": "Ainda nenhuma enviada",
  "Waiting to send": "À espera de envio",
  "Dropped, too many waiting": "Descartadas, demasiadas à espera",
  "Test sent. The result shows below within a few seconds.":
    "Teste enviado. O resultado aparece abaixo dentro de alguns segundos.",
  "Summary sent. The result shows below within a few seconds.":
    "Resumo enviado. O resultado aparece abaixo dentro de alguns segundos.",
  connected: "ligado",
  connecting: "a ligar",
  // sensor.js
  "Reading the history...": "A ler o histórico...",
  "Could not read the history: {why}": "Não foi possível ler o histórico: {why}",
  "Could not read the history.": "Não foi possível ler o histórico.",
  Today: "Hoje",
  "{n} alert in the last 7 days, by hour of the day (local time).":
    "{n} alerta nos últimos 7 dias, por hora do dia (hora local).",
  "{n} alerts in the last 7 days, by hour of the day (local time).":
    "{n} alertas nos últimos 7 dias, por hora do dia (hora local).",
  "No alerts in the last 7 days.": "Nenhum alerta nos últimos 7 dias.",
  "Not enough readings yet: the hub keeps one every 6 hours.":
    "Ainda não há leituras suficientes: a central guarda uma a cada 6 horas.",
  "No such sensor": "Esse sensor não existe",
  "Silent: not heard for too long": "Em silêncio: não é ouvido há demasiado tempo",
  "{s} s to reach the hub": "{s} s a chegar à central",
  "Test alert sent. The NVR should now record it: check that the recording is there.":
    "Alerta de teste enviado. O NVR deve gravá-lo agora: confirme que a gravação lá está.",
  // pictures.js
  "{file} is not the published file, so it was not run":
    "{file} não é o ficheiro publicado, por isso não foi executado",
  ACTIVE: "ATIVO",
  APPROACHING: "A APROXIMAR-SE",
  "MOVING AWAY": "A AFASTAR-SE",
  "LEFT TO RIGHT": "DA ESQUERDA PARA A DIREITA",
  "RIGHT TO LEFT": "DA DIREITA PARA A ESQUERDA",
  "PAST EVENT - CHECK LOGS": "EVENTO PASSADO - VER REGISTOS",
  "Name (first line)": "Nome (primeira linha)",
  Approaching: "A aproximar-se",
  "Moving away": "A afastar-se",
  "Past event (last line)": "Evento passado (última linha)",
  "Could not load the encoder from the internet: {why}": "Não foi possível carregar o codificador da internet: {why}",
  "The encoder did not produce a complete picture": "O codificador não produziu uma imagem completa",
  "The encoder failed": "O codificador falhou",
  "Even at the lowest quality this picture is over 32 KB. Try a simpler image.":
    "Mesmo com a qualidade mais baixa esta imagem tem mais de 32 KB. Experimente uma imagem mais simples.",
  "Optional text on top, up to 3 lines": "Texto opcional por cima, até 3 linhas",
  "Encoded picture": "Imagem codificada",
  "Choose an image. Any shape or size is fine.": "Escolha uma imagem. Qualquer forma ou tamanho serve.",
  "Encode and preview": "Codificar e pré-visualizar",
  "Save to the hub": "Guardar na central",
  "Add text if you like, then encode.": "Acrescente texto se quiser, e depois codifique.",
  "This browser cannot open that file.": "Este navegador não consegue abrir esse ficheiro.",
  "Encoding. The first time, this downloads about 30 MB.":
    "A codificar. Da primeira vez, isto descarrega cerca de 30 MB.",
  "{size}. Quality was lowered {n} of {steps} steps to fit 32 KB.":
    "{size}. A qualidade foi reduzida {n} de {steps} passos para caber em 32 KB.",
  "{size} at full quality.": "{size} com a qualidade máxima.",
  "The preview could not be shown, but the picture can still be saved.":
    "Não foi possível mostrar a pré-visualização, mas a imagem pode ser guardada na mesma.",
  "The hub refused this browser's video format. Please report this header: {header}":
    "A central recusou o formato de vídeo deste navegador. Por favor comunique este cabeçalho: {header}",
  "Getting the encoder ready. The first time, this downloads about 30 MB.":
    "A preparar o codificador. Da primeira vez, isto descarrega cerca de 30 MB.",
  "Making picture {n} of {total}": "A criar a imagem {n} de {total}",
  "Saved {n} picture": "{n} imagem guardada",
  "Saved {n} pictures": "{n} imagens guardadas",
  // system.js
  "Not set yet": "Ainda por acertar",
  "Read from this PC. Press Save to set the hub to this time.":
    "Lida deste PC. Carregue em Guardar para acertar a central com esta hora.",
  "Enter the date and time, or use Read from this PC.": "Indique a data e a hora, ou use Ler deste PC.",
  "Enter a time.": "Indique uma hora.",
  "The video stops for a few seconds.": "O vídeo para durante alguns segundos.",
  "Restart the hub now?": "Reiniciar a central agora?",
  Restart: "Reiniciar",
  "The hub is restarting.": "A central está a reiniciar.",
  "Every sensor node stops being heard until it is set up or paired again, and HTTP sensors need the new token. The hub restarts.":
    "Nenhum nó sensor volta a ser ouvido até ser configurado ou emparelhado outra vez, e os sensores HTTP precisam do novo token. A central reinicia.",
  "Make new keys?": "Criar novas chaves?",
  "New keys are being made. The hub is restarting.": "Estão a ser criadas novas chaves. A central está a reiniciar.",
  "The saved Wi-Fi networks, ports, name, web password, fixed address and time settings are erased. The hub restarts and may move to another address.":
    "As redes Wi-Fi guardadas, as portas, o nome, a palavra-passe web, o endereço fixo e as definições de hora são apagados. A central reinicia e pode mudar de endereço.",
  "Reset the settings?": "Repor as definições?",
  "Settings erased. The hub is restarting. If it cannot join a network, it opens its setup network.":
    "Definições apagadas. A central está a reiniciar. Se não conseguir entrar numa rede, abre a sua rede de configuração.",
  "Download crash report": "Descarregar relatório de falha",
  "Delete crash report": "Apagar relatório de falha",
  "Delete the crash report?": "Apagar o relatório de falha?",
  "Deleted the crash report": "Relatório de falha apagado",
  "For diagnosis with the firmware it came from. It holds a copy of the hub's memory, which can include passwords.":
    "Para diagnóstico com o firmware de onde veio. Contém uma cópia da memória da central, que pode incluir palavras-passe.",
  "This file is damaged, or is not a full backup from the hub.":
    "Este ficheiro está danificado, ou não é uma cópia completa da central.",
  "This file is cut short, or is not a full backup from the hub.":
    "Este ficheiro está incompleto, ou não é uma cópia completa da central.",
  "Type a passphrase of 8 characters or more, at the top of Backup and restore.":
    "Escreva uma frase-passe com 8 caracteres ou mais, no topo de Cópia de segurança e restauro.",
  "The two passphrases differ.": "As duas frases-passe são diferentes.",
  "Sealing with your passphrase: {n}%": "A selar com a sua frase-passe: {n}%",
  "The hub could not send its settings.": "A central não conseguiu enviar as definições.",
  "This backup is damaged, or was made by a newer hub.":
    "Esta cópia está danificada, ou foi feita por uma central mais recente.",
  "Saved, sealed with your passphrase. Keep the passphrase apart from the file.":
    "Guardada, selada com a sua frase-passe. Guarde a frase-passe longe do ficheiro.",
  "Collected {n} files": "{n} ficheiros recolhidos",
  "Saved {n} files, the settings and node keys sealed with your passphrase.":
    "{n} ficheiros guardados, com as definições e as chaves dos nós seladas com a sua frase-passe.",
  "Choose a full backup .tar file first.": "Escolha primeiro o ficheiro .tar de uma cópia completa.",
  "Its sensors, pictures and names are added, replacing any with the same name, then the settings are replaced and the hub restarts. It may move to another address.":
    "Os sensores, as imagens e os nomes são acrescentados, substituindo os que tiverem o mesmo nome, depois as definições são substituídas e a central reinicia. Pode mudar de endereço.",
  "Restore this backup?": "Restaurar esta cópia?",
  Restore: "Restaurar",
  "This is not a full backup from the hub.": "Isto não é uma cópia completa da central.",
  "Restoring {n} of {total}: {file}": "A restaurar {n} de {total}: {file}",
  "Restoring the settings": "A restaurar as definições",
  "The sensors and pictures were restored, but not the settings: {why}.":
    "Os sensores e as imagens foram restaurados, mas as definições não: {why}.",
  "the hub refused them": "a central recusou-as",
  "Restored {n} of {total} items and the settings. The hub is restarting.":
    "Restaurados {n} de {total} itens e as definições. A central está a reiniciar.",
  "Not restored: {list}": "Não restaurados: {list}",
  "Choose a firmware .bin file first.": "Escolha primeiro um ficheiro .bin de firmware.",
  "The hub restarts when it finishes, and the video stops until then.":
    "A central reinicia quando acabar, e o vídeo para até lá.",
  "Update the firmware?": "Atualizar o firmware?",
  Update: "Atualizar",
  "Uploading: {n}%. Keep this page open.": "A enviar: {n}%. Mantenha esta página aberta.",
  "Checking the signature": "A verificar a assinatura",
  "Done. The hub is restarting; this page reloads when it is back.":
    "Feito. A central está a reiniciar; esta página recarrega quando ela voltar.",
  "The upload failed. The hub keeps its current firmware.": "O envio falhou. A central mantém o firmware atual.",
  "Choose a backup file first.": "Escolha primeiro um ficheiro de cópia de segurança.",
  "The hub restarts and may move to another address.": "A central reinicia e pode mudar de endereço.",
  "Replace the hub's settings with this backup?": "Substituir as definições da central por esta cópia?",
  "Restored. The hub is restarting.": "Restaurada. A central está a reiniciar.",
  "The hub refused this backup": "A central recusou esta cópia",
  // the hub's Device info, restart reasons and connections; with {name}, patterns for messages that change
  "Arduino core": "Núcleo Arduino",
  Crash: "Falha",
  "Device name": "Nome do aparelho",
  "ESP-IDF": "ESP-IDF",
  "ESP-NOW messages dropped while busy": "Mensagens ESP-NOW descartadas por estar ocupada",
  "Firmware built": "Firmware compilado",
  "Firmware check": "Verificação do firmware",
  "Firmware version": "Versão do firmware",
  "Fixed address could not reach the router": "O endereço fixo não chegou ao router",
  "Flash size": "Tamanho da flash",
  "Free memory": "Memória livre",
  "Free memory pieces": "Blocos de memória livre",
  "Largest free block": "Maior bloco livre",
  "Last crash": "Última falha",
  "Last restart": "Último reinício",
  "Low memory": "Pouca memória",
  "Low power supply voltage": "Tensão de alimentação baixa",
  "Lowest free memory": "Menor memória livre",
  "Memory in use": "Memória em uso",
  "Memory in use by size": "Memória em uso por tamanho",
  "New WiFi settings": "Novas definições de Wi-Fi",
  "New firmware, being checked": "Firmware novo, em verificação",
  "New keys": "Novas chaves",
  "New web, port or address settings": "Novas definições web, de porta ou de endereço",
  Other: "Outro",
  "Power on or reset button": "Ligação à corrente ou botão de reinício",
  "Program size": "Tamanho do programa",
  "Restart button": "Botão de reinício",
  "Router stopped answering": "O router deixou de responder",
  "Server stalled": "O servidor parou",
  "Settings reset": "Definições repostas",
  "Settings restored": "Definições restauradas",
  "Settings storage": "Espaço das definições",
  "Smallest largest free block": "Menor dos maiores blocos livres",
  "Software restart": "Reinício por software",
  "The last update failed and was undone": "A última atualização falhou e foi desfeita",
  Uptime: "Tempo ligada",
  Watchdog: "Watchdog",
  "WiFi lost for 5 minutes": "Wi-Fi perdido durante 5 minutos",
  "WiFi network": "Rede Wi-Fi",
  "WiFi signal": "Sinal Wi-Fi",
  "Work dropped for lack of memory": "Trabalho descartado por falta de memória",
  brownout: "quebra de tensão",
  "closed without a reply": "fechada sem resposta",
  "could not connect": "não foi possível ligar",
  "could not connect to the broker": "não foi possível ligar ao broker",
  crash: "falha",
  "power on": "ligação à corrente",
  "reset button": "botão de reinício",
  software: "software",
  "the broker address is not an IP address": "o endereço do broker não é um endereço IP",
  "the broker closed the connection": "o broker fechou a ligação",
  "the broker did not acknowledge an event": "o broker não confirmou um evento",
  "the broker sent something that is not MQTT": "o broker enviou algo que não é MQTT",
  "the broker stopped answering": "o broker deixou de responder",
  "the connection failed": "a ligação falhou",
  "the connection to the broker failed": "a ligação ao broker falhou",
  watchdog: "watchdog",
  "{n} times since the start; the hub kept running": "{n} vezes desde o arranque; a central continuou a funcionar",
  "{n}, which their sensors resend": "{n}, que os sensores voltam a enviar",
  "{a} KB of {b} KB": "{a} KB de {b} KB",
  "{a} of {b} entries used": "{a} de {b} entradas usadas",
  "{n} cores at {m} MHz": "{n} núcleos a {m} MHz",
  "{n} KB since the start, checked each second": "{n} KB desde o arranque, verificado a cada segundo",
  "{model} revision {n}": "{model} revisão {n}",
  // notify.js, whether Home Assistant may disarm
  "Disarming from Home Assistant": "Desarmar a partir do Home Assistant",
  "Allowed, as the hub logs in to the broker": "Permitido, porque a central entra no broker com um acesso",
  "Refused: give the broker a login, so not anyone on the network can disarm the hub":
    "Recusado: dê um acesso ao broker, para que não seja qualquer pessoa na rede a poder desarmar a central",
  // the messages' language, under Notifications
  "Language of the messages": "Idioma das mensagens",
  "the language must be en or pt": "o idioma tem de ser en ou pt",
  // the hub's messages when saving fails, and the details of two hub events
  "could not create the folder": "não foi possível criar a pasta",
  "could not delete the crash report": "não foi possível apagar o relatório de falha",
  "could not delete the file": "não foi possível apagar o ficheiro",
  "could not do that": "não foi possível fazer isso",
  "could not save it; storage may be full": "não foi possível guardar; o armazenamento pode estar cheio",
  "could not save the MQTT settings": "não foi possível guardar as definições de MQTT",
  "could not save the NVR address": "não foi possível guardar o endereço do NVR",
  "could not save the Wi-Fi settings": "não foi possível guardar as definições de Wi-Fi",
  "could not save the address": "não foi possível guardar o endereço",
  "could not save the name; storage may be full": "não foi possível guardar o nome; o armazenamento pode estar cheio",
  "could not save the notification settings": "não foi possível guardar as definições das notificações",
  "could not save the preview; storage may be full":
    "não foi possível guardar a pré-visualização; o armazenamento pode estar cheio",
  "could not save the schedule": "não foi possível guardar o horário",
  "could not save the setting": "não foi possível guardar a definição",
  "could not save the setting; storage may be full":
    "não foi possível guardar a definição; o armazenamento pode estar cheio",
  "could not save the settings; storage may be full":
    "não foi possível guardar as definições; o armazenamento pode estar cheio",
  "could not save the text; storage may be full": "não foi possível guardar o texto; o armazenamento pode estar cheio",
  "could not save the time settings": "não foi possível guardar as definições de hora",
  "the hub's clock is not set, or it is short of memory":
    "o relógio da central não está acertado, ou tem pouca memória",
  "{n} times in an hour": "{n} vezes numa hora",
  "{n} nodes": "{n} nós",
  // the Overview's backup step
  "A sealed backup of the settings and the nodes' keys":
    "Uma cópia de segurança selada das definições e das chaves dos nós",
  "A new backup, as a node was paired since the last": "Uma cópia nova, porque um nó foi emparelhado depois da última",
  "A backup in the last month (the last was {ago})": "Uma cópia no último mês (a última foi {ago})",
  "Make a backup": "Fazer uma cópia",
  "A sealed backup holds the settings and each paired node's key. Without one, a hub that fails or is taken means setting it up and pairing every node again. Keep the file and its passphrase apart, and make a new one after pairing a node.":
    "Uma cópia selada guarda as definições e a chave de cada nó emparelhado. Sem ela, uma central que avarie ou seja levada obriga a configurá-la e a emparelhar todos os nós outra vez. Guarde o ficheiro e a frase-passe em sítios diferentes, e faça uma nova depois de emparelhar um nó.",
  // the hub's label, on Device info
  "The hub's label": "A etiqueta da central",
  "A label to stick on the hub: where its page is, how its setup network opens, and the fingerprint a node is paired against. Whoever finds the hub later knows what it is and how to reach it.":
    "Uma etiqueta para colar na central: onde está a sua página, como abre a rede de configuração, e a impressão digital com que se emparelha um nó. Quem encontrar a central mais tarde sabe o que é e como lhe chegar.",
  "Print a label for the hub": "Imprimir uma etiqueta para a central",
  "Alarm hub for ONVIF NVRs. Log in as admin with the hub password, kept elsewhere.":
    "Central de alarme para NVR ONVIF. Entre como admin com a palavra-passe da central, guardada noutro sítio.",
  "Page: {address} or {name}": "Página: {address} ou {name}",
  "Setup network {network}: opens when the hub cannot join its Wi-Fi, with the hub password; on a hub with no settings, press its BOOT button first.":
    "Rede de configuração {network}: abre quando a central não consegue entrar no seu Wi-Fi, com a palavra-passe da central; numa central sem definições, carregue primeiro no botão BOOT.",
  "Fingerprint for pairing nodes: {fingerprint}": "Impressão digital para emparelhar nós: {fingerprint}",
  "Serial number {serial}": "Número de série {serial}",
};
