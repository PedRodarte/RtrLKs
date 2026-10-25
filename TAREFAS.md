# Lista de tarefas

## Para você (amanhã)

- [ ] **Instalar o app no celular:** abra https://pedrodarte.github.io/RtrLKs/app/ no Android e siga os 5 passos.
- [ ] **Testar o fluxo completo com a tag:** parear (código na tela da tag) → proteção ligada (LED verde
      respirando) → afastar o celular ou desligar o Bluetooth por 20 s (alarme no celular, PERDIDO na tag)
      → *Estou chegando* → *Encontrei a criança*.
- [ ] **Testar com a tela apagada:** celular no bolso por alguns minutos; o heartbeat precisa continuar.
- [ ] **Anotar o consumo de bateria** depois de algumas horas com a proteção ligada.
- [ ] **Decidir se o repositório fica público** (o histórico antigo ainda tem o nome do Lucas).

## Para o Claude trabalhar no computador sem você

O que hoje faz o trabalho parar esperando você, e como resolver:

- [ ] **Celular ligado ao PC para testar o app:** ative *Opções do desenvolvedor → Depuração USB* no Android
      e deixe o celular no cabo (ou use *Depuração sem fio*). Assim o Claude instala o app, roda os testes e lê
      os logs (`adb`) sem você. É o item mais importante para o app.
- [ ] **Permissões do Claude Code:** liberar de uma vez os comandos usados sempre (compilar, gravar a placa,
      git, adb). Pode ser pelo comando `/permissions` ou pedindo ao Claude para usar a skill
      *fewer-permission-prompts*.
- [x] **Evitar janelas de administrador (UAC):** Java, Android SDK e Gradle foram instalados em versão portátil
      em `C:\Android`, sem precisar de administrador. Manter esse padrão.
- [ ] **PC sem suspender enquanto o Claude trabalha:** *Configurações → Energia → Suspensão: Nunca* (na tomada).
- [ ] **Acompanhar e aprovar pelo celular:** digite `/remote-control` no Claude Code para continuar a sessão
      pelo app ou pelo claude.ai.
- [x] **Tag ligada no USB do PC (COM3):** permite gravar e ler o log da placa sem você.

## Pendências técnicas

- [ ] Testar o app em celular real: pareamento com o endereço privado da tag, reconexão automática, alarme
      com a tela bloqueada, comportamento no modo de economia de bateria de cada fabricante.
- [ ] Bateria do app: o heartbeat a cada 5 s mantém o processador acordado. Alternativa a avaliar: o firmware
      considerar "conexão ativa" como presença, e o app mandar heartbeat bem menos vezes.
- [ ] Configurar o Wi-Fi de casa pelo app (hoje só pelo menuconfig).
- [ ] Assinatura do APK: hoje usa a chave de depuração deste PC. Para a Play Store, criar uma chave de release.
- [ ] iPhone: fora da versão 01 (limites de Bluetooth em segundo plano do iOS).
- [ ] Fase 2: localização da tag sem depender de quem a encontrar (servidor ou modem 4G com GPS).
