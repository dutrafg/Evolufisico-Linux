# EvoluFisico Linux

Aplicativo desktop **nativo para Linux**, em desenvolvimento, com interface GTK4, banco SQLCipher e funcionalidades de clientes, avaliações, cobranças e backups criptografados.

> **Prévia técnica. Não usar dados clínicos reais.** Ainda faltam controles individuais de acesso, homologação em Linux físico, análise independente de segurança e testes de recuperação em falhas.

## Começar

No Ubuntu 24.04 ou Debian compatível, instale as dependências e compile a aplicação:

```bash
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build pkg-config libgtk-4-dev libsqlcipher-dev
meson setup linux/build linux --prefix=/usr
meson compile -C linux/build
meson test -C linux/build --print-errorlogs
./linux/build/evolufisico-linux
```

Para gerar o instalador `.deb`:

```bash
bash linux/package-deb.sh
```

### Instaladores específicos por versão do Ubuntu (prévia 0.1.2)

**Não existe um único `.deb` compatível com todas as versões do Linux.** O projeto gera dois instaladores distintos na seção GitHub Actions:

- **Ubuntu 24.04 LTS**: baixe `evolufisico-linux-gtk4-deb-ubuntu24.04`, que contém `evolufisico-linux_0.1.2+ubuntu24.04_amd64.deb` e usa `libsqlcipher1`.
- **Ubuntu 26.04 LTS**: baixe `evolufisico-linux-gtk4-deb-ubuntu26.04`, que contém `evolufisico-linux_0.1.2+ubuntu26.04_amd64.deb` e usa `libsqlcipher2`.

Depois de extrair o ZIP correspondente à sua distribuição, na pasta que contém o arquivo, instale via terminal:

```bash
cat /etc/os-release
sudo apt update
sudo apt install ./evolufisico-linux_0.1.2+ubuntu*_amd64.deb
```

**Confirme sua versão do Linux antes de escolher o pacote.** O `*` acima deve encontrar somente o arquivo `.deb` correto, na pasta extraída, e não versões antigas do pacote. Não use `dpkg --force-depends`, links simbólicos artificiais entre bibliotecas ou bibliotecas de outra versão do Ubuntu.

SQLCipher é disponibilizado pelo componente `universe` do Ubuntu. Se os repositórios da instalação não fornecem a biblioteca correta, confira as fontes APT antes de instalar. O instalador é testado em Ubuntu 24.04 e 26.04 dentro do GitHub Actions; Linux Mint, Debian, Ubuntu 22.04 e outras distribuições **ainda não estão homologadas**. Em particular, Ubuntu 22.04 tem GTK4 anterior ao mínimo exigido pelo projeto.

A aplicação continua uma **prévia sem homologação para dados clínicos reais**. A instalação manual do `.deb` não cria um repositório de atualizações automáticas.

Veja [documentação completa](linux/README.md) e [auditoria de QA e segurança](linux/QA-SECURITY-AUDIT-2026-10-19.md).

## Organização

- `linux/src/`: aplicativo GTK4 e persistência criptografada.
- `linux/tests/`: testes de regressão e segurança.
- `linux/data/`: integração com o ambiente gráfico Linux.
- `linux/package-deb.sh`: geração de pacote Debian.
- `.github/workflows/linux-gtk4.yml`: compilação, testes e pacote no GitHub Actions.

## Histórico e origem

Este repositório foi separado de [dutrafg/EvoluFisicoDesktop, branch `linux-gtk4`](https://github.com/dutrafg/EvoluFisicoDesktop/tree/linux-gtk4), usando como referência o commit `f448450562c8410e1b16ae4db629d5492ed9dbff` (19/10/2026).

A cópia transfere os **arquivos Linux e o estado funcional daquele commit**, sem alterar a branch `main` do projeto Windows. O histórico completo dos commits anteriores continua acessível no repositório de origem; **os commits anteriores não são reescritos/copied como histórico nativo deste novo repositório**.

## Licença e segurança

Não há licença aberta explicitamente declarada nesta migração. O código permanece sob as condições aplicáveis definidas pelo proprietário. Não versione senhas, bancos reais, backups ou arquivos privados.
