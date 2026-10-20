# EvoluFisico Linux — GTK4

Prévia de desenvolvimento **não adequada para dados reais ou uso clínico**.

## Compilação (Ubuntu/Debian)

```bash
sudo apt-get install -y build-essential meson ninja-build pkg-config libgtk-4-dev libsqlite3-dev
meson setup linux/build linux --prefix=/usr
meson compile -C linux/build
./linux/build/evolufisico-linux
```

## Pacote .deb

Execute `bash linux/package-deb.sh` a partir da raiz do repositório depois de instalar as dependências. O pacote gerado fica em `linux/dist/`.

## Escopo atual

- Janela GTK4 nativa com navegação e cadastro básico de clientes persistido em SQLite.
- Atalho `.desktop` e instalador Debian.
- Sem migração automática de dados do Windows.
- O SQLite atual **não é criptografado**. Não inserir dados pessoais reais.
- Autenticação, avaliações, cobranças, PDF, backup, criptografia e testes de uso real ainda precisam ser implementados.

O código Windows em `main` permanece inalterado.

## Persistência e recuperação (fase experimental)

- Banco: `$XDG_DATA_HOME/evolufisico/records.sqlite` (ou `~/.local/share/evolufisico/records.sqlite`).
- Diretório privado (0700), arquivo SQLite (0600), validação de integridade e operações preparadas.
- API interna: `ef_store_backup` cria uma cópia SQLite consistente em arquivo temporário, verifica e renomeia para o destino; `ef_store_restore` verifica o backup antes de restaurar.
- **Atenção:** backups e banco ainda não são criptografados; não inserir dados pessoais reais, não compartilhar os arquivos e não usar para atendimento.
- A interface de restauração ainda não está disponível: a API será exposta após confirmação, criptografia e fluxo seguro de recuperação.
- Os testes de recuperação utilizam dados fictícios, não substituem homologação real.

## Interface GTK4 — etapa 30

- Clientes: cadastro, listagem e pesquisa em tempo real.
- Configurações: verificar integridade, criar backup sem sobrescrever destino e restaurar com confirmação explícita.
- O mecanismo de restauração mantém uma cópia temporária em memória para tentar reverter falhas durante a cópia. **Não substitui um backup externo independente.**
- A edição de nomes e exclusão continuam disponíveis apenas na API interna; ainda não estão ligadas à interface.
- **Segurança:** dados e backups continuam sem criptografia. Não usar dados pessoais reais nem considerar esta versão pronta para produção.

## Etapas 31–40

- Compilação da confirmação de restauração corrigida.
- Interface de clientes permite editar nomes informando ID e novo nome; listagem principal exibe IDs.
- Backups exigem caminho absoluto e não sobrescrevem arquivos existentes.
- A API de recuperação e os testes ainda estão sujeitos a homologação CI e revisão de segurança.
- **Importante:** a versão continua experimental, sem criptografia de banco ou backup; use apenas dados fictícios.

## Auditoria de bugs — etapas 41–50

Revisão estática identificou e corrigiu:
- risco de sobrescrever backup existente por corrida entre verificar e renomear: publicação sem sobrescrita via hard link no mesmo diretório;
- restauração acidental usando o próprio arquivo do banco ativo;
- backup com esquema SQLite de versão incompatível ou sem colunas essenciais;
- abertura do banco redefinindo silenciosamente a versão do esquema;
- pesquisa que ocultava IDs necessários à edição;
- ausência de testes específicos para esses cenários.

**Limitações:** sem criptografia, autenticação e testes de interface em desktop Linux real. A cópia de rollback da restauração está em memória e não garante recuperação diante de falha de energia ou encerramento do processo. A criação de backup não substitui estratégia de backup criptografado e externo. Não usar dados pessoais reais.

## Etapas 51–60 — modelos de avaliações e cobranças

- Banco SQLite inclui tabelas `assessments` e `charges`, ligadas a `clients` por chaves estrangeiras.
- Avaliações: data ISO, peso (kg), altura (cm), validação de limites e datas.
- Cobranças: valor armazenado em centavos inteiros e descrição.
- Backups preservam os novos dados; restauração rejeita esquemas sem as tabelas.
- **As novas operações ainda são APIs internas, não formulários GTK4.**
- **Nenhuma criptografia foi implementada nesta rodada.** Não utilizar dados reais, senhas ou informações clínicas. Não confundir validação de entradas com conformidade LGPD.

## Etapas 61–70 — formulários GTK4

- Avaliações: ID de cliente, data ISO, peso em kg, altura em cm, gravação e consulta de histórico.
- Cobranças: ID de cliente, valor em **centavos inteiros**, descrição, gravação e consulta.
- A seleção por ID é provisória. Não há autenticação, criptografia, relatórios PDF ou validação visual em desktop físico.
- Não use dados reais de pacientes ou clientes. Os testes usam registros fictícios.

## Etapas 71–80 — seleção de clientes

- Avaliações e cobranças passam a usar seletores GTK4 com ID estável e nome do cliente.
- A seleção é atualizada após operações de cadastro e edição, preservando o cliente selecionado quando ainda existir.
- Nenhum cliente é selecionado automaticamente: é preciso escolher explicitamente.
- O aplicativo permanece sem criptografia e sem autorização para uso com dados reais.

## Etapas 81–100 — gestão de registros e QA

- Listagens de avaliações e cobranças exibem IDs individuais.
- Exclusão de avaliações e cobranças exige confirmação explícita.
- Interface verifica que o registro pertence ao cliente selecionado antes de excluir.
- Testes cobrem exclusões válidas, IDs inválidos e tentativas de operar em outro cliente.
- Índices SQLite otimizam consultas por cliente.
- **Risco residual:** a API interna de exclusão por ID não incorpora cliente; a validação está na interface e pode ser melhorada com exclusão SQL atômica por `id` e `client_id`.
- Banco e backups seguem **sem criptografia**. Uso restrito a dados fictícios.

## Etapas 101–113 — revisão de segurança e usabilidade

- Permissões locais restritas a usuário, bloqueio de links simbólicos para banco e backups.
- Exclusão de cliente com confirmação explícita e aviso de exclusão em cascata.
- Listas roláveis, data de avaliação pré-preenchida e limpeza de formulário após salvar.
- Verificação da versão de esquema antes de criar tabelas.
- Testes adicionais de permissões, symlinks, versões e compilação estrita.
- **Não há criptografia de dados em repouso**; permissões POSIX não substituem criptografia, autenticação ou controles LGPD.

## Etapas 115–134 — modernização GTK4 e compilação estrita

- Seletor de clientes migrado de `GtkComboBoxText` para `GtkDropDown` e `GtkStringList`.
- A seleção permanece por ID estável e não escolhe automaticamente o primeiro cliente.
- Correções de erros nulos, confirmação de exclusão e formatação portável de inteiros SQLite.
- Compilação estrita mantém **exceção temporária somente para APIs GTK4 de diálogo obsoletas**; a migração dos diálogos ainda é pendente.
- QA cobre datas inválidas, IDs negativos, integridade, backup e restore.
- Banco SQLite e backups seguem sem criptografia; usar apenas dados fictícios.

## Segurança experimental SQLCipher e GTK4 (outubro de 2026)

- O banco passou a usar **SQLCipher** e exige `EVOLUFISICO_DB_PASSPHRASE` com pelo menos 12 caracteres. Sem chave ou com chave errada, a abertura é recusada. **Há agora um diálogo GTK4 de desbloqueio por senha**. Ainda não há contas individuais, permissões por usuário ou recuperação de senha.
- **Atenção:** variáveis de ambiente podem expor a senha a processos e ferramentas de diagnóstico. Não coloque senhas em scripts versionados. A próxima fase deve eliminar o caminho legado por variável de ambiente e integrar armazenamento seguro de chaves (Secret Service/libsecret), com testes de segurança.
- Bancos SQLite legados sem criptografia são recusados. Não há migração automática; preserve backups anteriores até existir um migrador seguro e auditado.
- Backups são cópias do arquivo já criptografado, verificadas com SQLCipher. A restauração utiliza cópia de reversão e substituição do arquivo, com verificações de integridade.
- Diálogos de exclusão e restauração usam `GtkAlertDialog` (GTK 4.10+).
- CI executa captura de tela via Xvfb e publica um artefato visual. **Xvfb não é um computador Linux físico e não prova interação visual manual**.
- A proteção permanece experimental: faltam testes de concorrência, recuperação após queda de energia, políticas de credenciais, migração, autenticação de usuários e homologação presencial. Não utilizar dados clínicos reais.

## Rodada final de endurecimento e homologação

- A tela de desbloqueio limpa a entrada após erro e limita cinco tentativas malsucedidas com espera de 30 segundos (somente por processo, não é proteção contra ataques offline).
- A cópia de backup criptografado recusa modo SQLite WAL para não ignorar transações existentes no arquivo auxiliar.
- O teste automatizado de interface utiliza Xvfb e gera screenshot no GitHub Actions. Para homologação em Linux físico ainda são necessários instalação real, interação humana com todas as páginas, verificação de DPI e fontes, reinício, backup e restauração em disco externo, testes de suspensão e desligamento inesperado.
- **Atenção:** ainda não há contas individuais, controle de acesso por perfil, cofre de credenciais, migração de banco legado ou recuperação segura de senha. Uma única senha protege o banco local. O sistema permanece **não homologado para dados clínicos reais**.

## Auditoria de pendências — bloqueadores de produção

O aplicativo Linux está em estágio de **prévia técnica**. O pipeline automatizado não equivale à certificação de segurança, nem à homologação LGPD. Não inserir dados clínicos reais.

| Item | Situação |
| --- | --- |
| SQLCipher e backups criptografados | Implementados; testes automatizados de chave incorreta, cabeçalho cifrado, integridade e restauração |
| Backup concorrente | Bloqueio SQLite `BEGIN IMMEDIATE` durante cópia e recusa do modo WAL; testar também processos externos e quedas de energia |
| Autenticação individual e permissões por usuário | **Pendente** — desbloqueio local usa uma senha compartilhada |
| Armazenamento de segredo com libsecret | **Pendente** — senha ainda pode ser fornecida por variável de ambiente ou ficar em memória do processo |
| Migração de SQLite antigo sem criptografia | **Pendente** — versão atual recusa migração automática; preserve cópias legadas |
| Recuperação de senha | **Pendente** — não há mecanismo seguro de recuperação; perda da senha implica perda de acesso |
| Segurança e privacidade | **Pendente** — revisão independente, modelo de ameaças, política de retenção e controles LGPD |
| QA gráfico | Xvfb automatizado concluído; **homologação em Linux físico pendente** |
| Testes de falha | **Pendente** — interrupção de energia, falta de espaço, disco removido, concorrência multiprocesso, backup externo |
| Instalação real | **Pendente** — Ubuntu/Debian e demais distribuições, upgrades, dependências e remoção segura |

**Critério para produção:** concluir e registrar evidências de todos os itens pendentes, executar teste de recuperação real com backups fictícios e obter aprovação de segurança e privacidade.

## Correções Windows adaptadas ao Linux — 19/10/2026

- **Cobranças pendentes:** status `pendente` por padrão, ação `Receber cobrança pendente...` com confirmação GTK4 e transição SQLCipher atômica para `pago` com data UTC. Uma segunda tentativa de recebimento falha sem modificar a cobrança.
- **Migração do banco:** `user_version=2` acrescenta status e data de recebimento a bases SQLCipher anteriores (versão 1), preservando os registros. Backups das versões 1 e 2 são reconhecidos.
- **Avaliações:** o Linux já grava diretamente no SQLite criptografado; a falha JavaScript de `perimetria_soma.toFixed` não se aplica ao formulário nativo atual (que só possui data, peso e altura).
- **Diferença funcional ainda existente:** o Linux não oferece a escolha e sincronização de pasta física de avaliações do Windows. Portanto, a correção de gravação duplicada na pasta não pode ser considerada portada. A função requer implementação própria, permissões e testes de sincronização.
- **Uso experimental:** não inserir dados clínicos reais até a homologação de segurança e privacidade.

## Etapas adicionais 1–10 — correções Windows portadas (19/10/2026)

- **Cobranças:** já adaptado e validado no Linux: pagamento pendente para pago, com registro de recebimento e proteção contra repetição.
- **Avaliações:** ao registrar avaliação, a interface agora aceita opcionalmente um caminho absoluto de pasta existente. Depois da gravação no SQLCipher local, cria um **snapshot criptografado de todo o banco** nessa pasta, com nome único e permissões `0600`. Os dois resultados são informados separadamente: falha na cópia não desfaz nem oculta o salvamento local.
- A pasta não recebe arquivos individuais legíveis de avaliação: são snapshots SQLCipher do banco inteiro, que exigem a senha original. Não compartilhar essa pasta com pessoas sem autorização.
- **Limite importante:** esta etapa implementa cópia automática após salvar avaliações **enquanto o aplicativo estiver aberto**. Ainda **não** há monitoramento bidirecional da pasta, importação automática de alterações feitas externamente, agendamento em segundo plano ou sincronização entre dispositivos. Não anunciar sincronização completa.
- QA cobre cópia válida, permissões do arquivo, pasta inexistente, caminho relativo e link simbólico. A proteção contra concorrência ainda requer homologação multiprocesso e teste de interrupção de energia.

## Configuração persistente da pasta de cópias — primeira etapa Insync

- Na tela **Configurações**, use **Selecionar pasta...** para escolher um diretório local ou uma pasta de nuvem **já montada** pelo sistema.
- O caminho é validado e salvo em `$XDG_CONFIG_HOME/evolufisico/sync-folder.ini` (normalmente `~/.config/evolufisico/sync-folder.ini`), com permissões restritas. Não são armazenados tokens de provedores.
- A escolha é restaurada no próximo início do aplicativo e aparece no campo de pasta da tela **Avaliações**. Esse campo é somente leitura para evitar configurações temporárias divergentes.
- **Verificar pasta** testa disponibilidade e permissões. Se o drive for desconectado, o caminho configurado não é apagado, mas a verificação alerta que está indisponível. **Remover seleção** esquece o caminho sem excluir backups.
- Ao salvar uma avaliação com pasta configurada e acessível, o fluxo existente cria um snapshot **SQLCipher** do banco, sem sobrescrever cópias anteriores, e informa separadamente eventual falha de cópia.
- **Isso ainda não é uma sincronização contínua ou bidirecional** e não transfere arquivos diretamente ao Google Drive; a pasta de nuvem precisa estar montada por outro programa. Não há monitoramento em segundo plano nem tratamento de conflitos.
- O teste automático `folder-settings` verifica persistência, reinício simulado, permissões, caminhos relativos/inexistentes, symlinks, versões incompatíveis e desconexão temporária.

### Privacidade

As configurações guardam apenas o caminho local. O banco e os snapshots são cifrados por SQLCipher, mas **metadados de nome e quantidade de backups ficam visíveis no sistema de arquivos**. Não use dados reais até a homologação Linux e revisão independente de segurança.


## Cópias automáticas criptografadas após alterações — etapa 2 Insync

- Depois que uma pasta de destino for configurada em **Configurações**, cada gravação feita **pela interface GTK4** cria automaticamente um **novo snapshot completo SQLCipher** com nome único nessa pasta; arquivos existentes não são sobrescritos.
- Os gatilhos incluem: cadastrar, renomear e excluir cliente; registrar e excluir avaliação; registrar, receber e excluir cobrança; restaurar backup. Exclusões continuam exigindo confirmação.
- O **banco local sempre é salvo primeiro**. Se a pasta estiver desmontada, sem permissão, cheia ou o snapshot falhar, a operação local permanece concluída; o usuário vê uma mensagem de falha da cópia distinta do resultado do banco. A cópia não é refeita automaticamente enquanto a pasta estiver indisponível (pendente de etapa posterior).
- Sem pasta selecionada, o aplicativo grava apenas no banco local e informa que as cópias automáticas estão desativadas.
- O snapshot de Avaliações não é gerado duas vezes: o tratamento foi centralizado e é compartilhado com Clientes e Cobranças.
- As cópias **não são espelhos bidirecionais**: operações diretamente no sistema de arquivos ou em outros aplicativos não disparam snapshots e não são importadas. Ainda não existe integração OAuth, daemon em segundo plano, integração com Google Drive nem resolução de conflitos.
- **Retenção:** cada mudança cria um arquivo distinto; ainda não existe limpeza/rotação automática. Monitore o espaço disponível e mantenha backups externos seguros.
- O teste `auto-backup` cobre nove cenários de cópia após alteração ou recuperação, permissões privadas, arquivo criptografado, desconexão de pasta, preservação do SQLCipher local e restauração.

**Prévia experimental**, sem homologação em máquina Linux física ou liberação para dados clínicos reais.


## Etapa 3 Insync — fila persistente e recuperação após reconexão

- **Fila local criptografada:** quando a pasta de destino é configurada, cada operação salva com sucesso no SQLCipher cria antes uma cópia criptografada em \`$XDG_DATA_HOME/evolufisico/pending-snapshots\` (normalmente \`~/.local/share/evolufisico/pending-snapshots\`). Só depois o aplicativo tenta transferi-la para a pasta selecionada.
- **Pasta indisponível:** a gravação no banco continua válida e a cópia permanece **PENDENTE** na fila local, com aviso distinto de perda de dados. Não há rollback da alteração somente porque a cópia falhou.
- **Retomada:** com o aplicativo aberto e desbloqueado, tenta reenviar cópias pendentes ao iniciar e a cada **20 segundos**. Há também o botão **Reenviar pendentes** em Configurações. As cópias são retomadas quando a pasta volta a estar disponível.
- **Reinício:** os arquivos cifrados na fila sobrevivem ao fechamento e reinício do processo; não ficam apenas em memória.
- **Sem sobrescrita:** cópias são verificadas pelo SQLCipher antes e depois da transferência. A transferência usa arquivo temporário, sincronização de disco e publicação sem substituir arquivos existentes. Caso o nome já exista com conteúdo diferente, a cópia continua pendente para revisão; se for idêntica, o reenvio é tratado como idempotente.
- **Concorrência:** uma trava local no processo de replay evita duas instâncias tentando enviar a mesma fila ao mesmo tempo. O tamanho da fila é limitado a **256 cópias**; ao atingir o limite, o usuário recebe erro explícito e precisa liberar espaço ou restabelecer o destino. Nenhuma cópia antiga é apagada automaticamente.
- **Sem pasta escolhida:** nenhuma cópia automática nova é enfileirada. Arquivos já pendentes são preservados e podem ser enviados quando uma nova pasta válida é selecionada.
- **Privacidade:** a fila contém cópias SQLCipher cifradas e deve permanecer privada. As cópias transferidas continuam exigindo a senha original do banco.

### Limitações e próximos bloqueadores

A retomada funciona **somente durante a execução do aplicativo**: ainda não existe daemon no login, sincronização com Google Drive via OAuth, comparação bidirecional de arquivos nem resolução de conflitos entre dispositivos. A interface GTK4 executa os envios de forma síncrona e pode ficar momentaneamente ocupada com arquivos muito grandes. Sistemas de arquivos que não suportem criação de links rígidos ou confirmação por \`fsync\` podem recusar transferências; a fila preserva as cópias até uma ação segura. Uma pasta montada que vire um diretório comum após remoção de disco exige ainda verificação explícita de identidade de montagem antes da homologação. **Não utilizar dados clínicos reais.**
