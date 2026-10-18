const fs = require('fs');

function createAccountStore(file) {
  const read = () => {
    if (!fs.existsSync(file)) return {};
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  };

  let accounts = read();

  return {
    get snapshot() {
      return accounts;
    },
    replace(nextAccounts) {
      accounts = nextAccounts;
    },
    save() {
      fs.writeFileSync(`${file}.tmp`, JSON.stringify(accounts), { mode: 0o600 });
      fs.renameSync(`${file}.tmp`, file);
    },
  };
}

module.exports = { createAccountStore };
