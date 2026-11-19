<p align="center">
  <img src="app/assets/icons/app/project-freedom.png" alt="Identidade visual do Project Freedom" width="240">
</p>

<h1 align="center">Project Freedom</h1>

<p align="center">
  Uma estação de áudio digital para criar, organizar, processar e mixar música.
</p>

O Project Freedom é um DAW open source para Windows e Linux, desenvolvido em C++20
com JUCE. Seu objetivo é reunir no próprio programa as ferramentas essenciais de
produção e uma coleção crescente de instrumentos e efeitos. Assim, instrumentos e
samples externos ficam a critério de cada pessoa — não são uma exigência para começar.

## Recursos atuais

- **Arranjo:** trilhas MIDI e de áudio, clips organizados na linha do tempo, loop e controles de transporte.
- **Piano roll:** criação e edição de notas, velocity, grade, quantização, escalas e ferramentas de seleção.
- **Mixer:** volume, pan, mute, solo e medição de nível por trilha, além do canal Master.
- **Drum Rack:** 16 pads para samples, com cadeias de efeitos por pad.
- **Plugins VST3:** busca de instrumentos e efeitos, cadeias de plugins e salvamento do estado no projeto.
- **Efeitos nativos:** ferramentas integradas como Volume Master e Pan Master.
- **MIDI Learn:** mapeamento de controles MIDI para volume, pan, mute e solo, com vínculos salvos por projeto.
- **Projetos e exportação:** salvar e abrir projetos `.pfs` e exportar o Master em WAV.
- **Personalização:** temas, configurações de áudio e seleção de entradas MIDI.

## Próximas grandes features

Estas etapas fazem parte da visão do Project Freedom e ainda estão em desenvolvimento:

1. **Editor de áudio completo:** ampliar as ferramentas de edição de clips, com cortes, divisão, ajuste de fades e operações mais precisas no arranjo.
2. **Gravação em tempo real:** gravar entradas de áudio diretamente em trilhas, com monitoramento e integração ao transporte.
3. **Sampler integrado e simples:** carregar e editar samples, ajustar reprodução e afinação e tocar o material pelo teclado ou MIDI.
4. **Automação de plugins:** selecionar parâmetros de instrumentos e efeitos e criar ou editar suas linhas de automação no arranjo.
5. **Mais efeitos integrados:** expandir a coleção nativa para oferecer ferramentas essenciais de processamento sem depender de plugins externos.

## Compilar

Requisitos: CMake 3.22+ e compilador C++20.

**Windows** — com Visual Studio e o componente “Desenvolvimento para desktop com C++”:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Executável: `build\app\ProjectFreedom_artefacts\Release\Project Freedom.exe`

**Linux (Ubuntu/Debian):**

```sh
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Execute os testes com:

```sh
ctest --test-dir build -C Release --output-on-failure
```

No navegador de plugins, **Pastas** permite adicionar ou remover diretórios de busca VST3.
Esses caminhos ficam salvos nas configurações do usuário.

## Estrutura

```text
core/    modelo musical e sequenciador
tests/   testes automatizados
app/     aplicativo JUCE, interface, áudio, plugins e ferramentas de produção
```
