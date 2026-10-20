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

### Instalação da prévia DEB 0.1.1 (Ubuntu 24.04)

Baixe o artefato `evolufisico-linux-gtk4-deb` da execução aprovada em **GitHub → Actions → EvoluFisico Linux GTK4 - CI** e extraia o arquivo `evolufisico-linux_0.1.1_amd64.deb`. A versão 0.1.0 tinha uma dependência incorreta em `libsqlcipher0`; a versão 0.1.1 utiliza `libsqlcipher1`, disponível no repositório *universe* do Ubuntu 24.04.

Dentro da pasta em que extraiu o pacote, execute:

```bash
sudo apt update
sudo apt install ./evolufisico-linux_0.1.1_amd64.deb
```

Caso `libsqlcipher1` não seja encontrado no Ubuntu, verifique se o repositório **universe** está habilitado (`sudo add-apt-repository universe && sudo apt update`) antes de tentar novamente. **Não** force dependências com `dpkg --force-depends`; a biblioteca SQLCipher é obrigatória para abrir o banco criptografado.

O instalador é compilado e testado no Ubuntu 24.04 x86_64. Em outras versões/distribuições o nome ou ABI das bibliotecas pode variar; faça homologação específica antes do uso. A instalação por arquivo local não configura atualizações automáticas do GitHub.


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
