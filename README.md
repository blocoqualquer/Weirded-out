# WEIRDED OUT (Weird Genesis)

RPG dungeon crawler de sobrevivencia para **PSP** (homebrew em C, PSPSDK),
com personagens 3D chibi e tom grotesco. Prototipo v0.1.

## O que ja existe
- Andares gerados proceduralmente (drunkard's walk + BFS), com escada e fogueira
- Personagens 3D chibi feitos de cubos (jogador + 3 inimigos: Gosma, Carnicento, Rato Cego)
- Combate por turnos (atacar, comer, bandagem, fugir), com emboscadas
- Sobrevivencia: fome, ferimentos (perna/braco), ciclo dia/noite (noite deixa inimigos mais atentos)
- Fogueira: descansa, cura, salva e **faz os monstros voltarem**
- Nivel/XP, baus, morte (volta para a ultima fogueira)

## Controles
| Botao | Acao |
| --- | --- |
| D-pad / analogico | andar (bater num inimigo/bau interage) |
| X | confirmar / descer escada / descansar na fogueira |
| Quadrado | comer racao |
| Triangulo | usar bandagem |

## Como compilar
Precisa do toolchain [pspdev](https://github.com/pspdev/pspdev):

```sh
export PSPDEV=/caminho/para/pspdev
export PATH=$PATH:$PSPDEV/bin
make
```

Isso gera `EBOOT.PBP`. Abra-o no emulador **PPSSPP** (Arquivo > Abrir arquivo).
O save fica em `save.dat`, na mesma pasta do EBOOT.

## Testes
```sh
tests/run.sh
```
Compila a logica no computador com stubs do PSP e deixa um robo jogar centenas
de milhares de frames com sanitizers, checando se o estado do jogo continua valido.

## Limitacoes conhecidas
- Textos sem acento (a fonte embutida do PSP nao tem acentos)
- Ainda sem audio, vila/fazenda, recrutamento ou corpo detalhado
- Graficos ainda nao foram conferidos visualmente em um emulador

## Proximos passos sugeridos
1. Conferir visual no PPSSPP e ajustar camera/cores
2. Vila com horta e comerciante (ciclo vila -> dungeon -> vila)
3. Party de 2 a 4 personagens e classes
4. Negociacao/recrutamento de monstros
5. Audio e fonte com acentos
