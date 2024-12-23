const tape = require('tape');
const fs = require('fs');
const hypc = require('../index.js');

tape('Deterministic Compilation', function (t) {
  t.test('DAO', function (st) {
    const input = {};
    let prevBytecode = null;
    const testdir = 'test/DAO/';
    const files = ['DAO.hyp', 'Token.hyp', 'TokenCreation.hyp', 'ManagedAccount.hyp'];
    let i;
    for (i in files) {
      const file = files[i];
      input[file] = { content: fs.readFileSync(testdir + file, 'utf8') };
    }
    for (i = 0; i < 10; i++) {
      const output = JSON.parse(hypc.compile(JSON.stringify({
        language: 'Hyperion',
        settings: {
          optimizer: {
            enabled: true
          },
          outputSelection: {
            '*': {
              '*': ['zvm.bytecode']
            }
          }
        },
        sources: input
      })));
      st.ok(output);
      st.ok(output.contracts);
      st.ok(output.contracts['DAO.hyp']);
      st.ok(output.contracts['DAO.hyp'].DAO);
      st.ok(output.contracts['DAO.hyp'].DAO.zvm.bytecode.object);
      const bytecode = output.contracts['DAO.hyp'].DAO.zvm.bytecode.object;
      st.ok(bytecode.length > 0);
      if (prevBytecode !== null) {
        st.equal(prevBytecode, bytecode);
      }
      prevBytecode = bytecode;
    }
    st.end();
  });
});
