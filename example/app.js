const cld = require('../index');

cld.detect('This is a language recognition example', function (err, result) {
	console.log(result);
});

cld.detect('', function (err, result) {
	console.log(err);
});
