// Standard JSON adapter used by the cross-platform bytecode comparison.
'use strict';
const fs = require('fs');
const path = require('path');
const compiler = require(path.resolve(process.argv[2]));
process.stdout.write(compiler.compile(fs.readFileSync(0, 'utf8')));
