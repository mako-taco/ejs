var _parse = require(require('path').join(__dirname, '../build/Release/parse.node'));

module.exports = function(str, options){
  var options = options || {}
    , open = options.open || exports.open || '<%'
    , close = options.close || exports.close || '%>'
    , filename = options.filename
    , str = str || ''
    , _with = options._with || (options._with === undefined) 
    , compileDebug = (options.compileDebug !== false)
    , buf = "";
  var obj = _parse.parse(str, open, close, filename, compileDebug, _with);
  return obj;
};