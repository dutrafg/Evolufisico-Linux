# Auditoria de QA, bugs e segurança — EvoluFisico Linux GTK4
Data: 2026-10-19
Escopo: branch `linux-gtk4` do repositório `dutrafg/EvoluFisicoDesktop`.
Natureza: auditoria de código e testes automatizados; **não é pentest independente nem homologação clínica**.

## Verificações automatizadas
- Compilação Meson e pacote Debian no Ubuntu 24.04.
- Compilação com avisos como erros.
- Testes do armazenamento SQLCipher (criação, leitura, atualização, exclusão, integridade e recuperação).
- Senha incorreta, backups cifrados, integridade de backup, restauração e arquivo corrompido.
- Entrada com SQL metacaracteres para regressão de injeção; UTF-8 inválido; números NaN e infinito.
- Validação de IDs e separação de registros por cliente.
- Tentativas de backup/restauração por links simbólicos, caminhos relativos e banco incompatível.
- Teste de backup em modo WAL e prevenção de gravação concorrente durante snapshot.
- UBSan e AddressSanitizer em testes nativos (detecção de vazamentos desativada para ASan).
- Verificações estáticas simples de chamadas C perigosas; **não equivalem a SAST especializado**.
- Inicialização GTK4 no Xvfb, screenshot e geração de .deb.

## Achados e correções
1. **QA / falha de teste:** suposição incorreta de que IDs de cobrança nunca são reutilizados. SQLite `INTEGER PRIMARY KEY` pode reutilizar IDs após exclusão; teste passou a consultar o ID real.
2. **QA / falha de teste:** teste de UTF-8 inválido usava caracteres literais de barra invertida, não um byte inválido; corrigido.
3. **Defesa adicional:** inclusão de testes para manipulação maliciosa de backups criptografados.
4. **Defesa adicional:** compilações instrumentadas com sanitizadores de memória e comportamento indefinido.

## Riscos que continuam abertos
- **Alto:** não há autenticação individual, papéis/permissões, trilha de auditoria de acessos ou gestão de sessão por usuário.
- **Alto:** segredo pode vir de variável de ambiente; não há integração com libsecret nem recuperação segura de senha.
- **Alto:** migração de bases legadas sem criptografia ainda não existe.
- **Alto:** restauração não tem testes de falha de energia, disco cheio, falha de fsync, concorrência entre processos e durabilidade de rollback.
- **Médio:** não há varredura de CVEs/SBOM de dependências e auditoria independente.
- **Médio:** UI visual automatizada no Xvfb não comprova acessibilidade, DPI, usabilidade ou comportamento em Linux físico.
- **Médio:** política de retenção, consentimento e controles LGPD ainda não foram homologados.

## Critério de aceite
CI da versão exata deve terminar com sucesso, incluindo os sanitizadores. Antes de usar dados reais, concluir controles de acesso e privacidade, revisar criptografia/backup por especialista e homologar em máquina Linux física com testes de recuperação.
