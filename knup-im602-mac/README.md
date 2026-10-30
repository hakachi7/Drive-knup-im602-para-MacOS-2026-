# Knup KP-IM602 para Mac

Driver para usar a impressora térmica **Knup KP-IM602 (80 mm, USB)** no macOS. A fabricante só oferece driver para Windows.

Depois de instalado, a impressora aparece como uma impressora comum no Mac. Você aperta **Cmd + P** em qualquer programa (Chrome, Safari, Visualização, sistemas de pedidos como FacilZap e VendiZap), e o recibo sai **com o mesmo layout da tela**, cortado no fim.

- Funciona em Macs **Apple Silicon (M1, M2, M3…) e Intel**, macOS 11 ou mais novo
- Acentos sempre corretos, porque tudo é impresso como imagem
- Remove o espaço em branco do fim e corta o papel automaticamente
- Opções no diálogo de impressão: corte, avanço antes do corte, escuridão, modo texto/foto, gaveta e bipe

> Na USB, a impressora se identifica como **POS-80C** (`usb://Printer/POS-80C`). Outras térmicas 80 mm com esse mesmo nome provavelmente também funcionam.

---

## Instalação

1. Ligue a impressora e conecte o cabo USB no Mac.
2. Baixe o arquivo **`Knup-IM602-Mac-x.x.x.pkg`** na página [**Releases**](../../releases/latest).
3. Dê dois cliques no arquivo e siga o instalador.

**Se o Mac disser que não pode abrir o instalador** ("desenvolvedor não identificado"):
abra **Ajustes do Sistema → Privacidade e Segurança**, role até o fim e clique em **Abrir mesmo assim**.
Isso acontece porque o instalador não tem a assinatura paga da Apple.

**A impressora não apareceu?** Com ela ligada, vá em **Ajustes do Sistema → Impressoras e Scanners → Adicionar impressora** e escolha a **POS-80C**. O Mac encontra o driver sozinho.

## Como imprimir

Aperte **Cmd + P**, escolha **Knup IM602 80mm** e use o papel **80mm - Rolo contínuo** (no Chrome aparece como *72 x 3.276 mm*).

No **Chrome**, abra **Mais definições** e ajuste:

| Opção | Valor |
|---|---|
| Margens | Nenhuma |
| Cabeçalhos e rodapés | desmarcado |
| Escala | Ajustar à área imprimível (ou *Personalizado 85–90%* se as colunas ficarem apertadas) |

O papel "rolo contínuo" é uma folha bem comprida. O driver corta logo depois do último conteúdo, então não gasta papel à toa.

## Problemas comuns

| Problema | Solução |
|---|---|
| Texto pequeno demais | O PDF foi feito em A4. Marque "Ajustar ao tamanho" / "Escala para caber". |
| Colunas encavaladas (ex.: código SKU em cima da descrição) | No Chrome, use Escala *Personalizado* 85 ou 90. A largura útil é 72 mm, limite físico da impressora. |
| Impressão clara ou falhada | Em "Ajustes" do diálogo de impressão, escolha Escuridão: *Escuro*. |
| Saiu papel demais | Desmarque *Cabeçalhos e rodapés* no Chrome. |
| Imprimiu e não cortou | Confira se *Cortar papel* está em "Depois de cada página". |

## Desinstalar

No Terminal:

```
sudo /Library/Printers/Knup/desinstalar.sh
```

---

## Como funciona (para quem quer saber)

O driver de Windows (*POS Printer Driver V11.3*) não manda texto para a impressora: ele desenha a página como **imagem a 203 dpi com 576 pontos de largura (72 mm)** e envia essa imagem com o comando ESC/POS `GS v 0`, em blocos de 24 linhas. Este projeto faz a mesma coisa no Mac:

1. O macOS (CUPS) transforma o documento em imagem a 203 dpi, usando as medidas do arquivo `ppd/Knup-IM602.ppd`.
2. O filtro `src/rastertoim602.c` converte a imagem em ESC/POS:
   - blocos `GS v 0` com 24 linhas cada;
   - remove o espaço em branco do começo e do fim, e encurta para no máximo 10 mm os espaços enormes (como o rodapé que o navegador põe no fim da folha);
   - `GS V 0` para cortar.

**Detalhe importante da IM602:** a impressora *encolhe sozinha* as linhas totalmente brancas e os avanços de papel feitos com `ESC J`, uma espécie de "economia de papel" interna. Com isso o espaçamento vertical sai errado. A solução foi enviar os espaços como parte da imagem, com um único ponto na última coluna de cada linha branca. Assim nenhuma linha fica 100% branca e o espaçamento sai igual ao da tela. Em `testes/teste-espaco.bin` está o teste que comprovou isso:

```
lp -d Knup_IM602 -o raw testes/teste-espaco.bin
```

### Compilar você mesmo

Precisa das Ferramentas de Linha de Comando da Apple (`xcode-select --install`):

```
bash tools/build-pkg.sh          # gera build/Knup-IM602-Mac-1.0.0.pkg
```

O GitHub Actions (`.github/workflows/build.yml`) gera o instalador automaticamente a cada envio. Quando você cria uma tag `v1.0.0`, ele também publica o instalador em **Releases**.

## Aviso

Projeto independente, sem ligação com a Knup. Use por sua conta e risco. Licença [MIT](LICENSE).
