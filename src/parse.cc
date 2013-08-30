#include <v8.h>
#include <node.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <deque>
#include <fstream>

#include <trim.h>

using namespace node;
using namespace v8;

struct parse_args {
  std::string str;
  std::string open;
  std::string close;
  std::string filename;
  std::string result;
  std::string err;
  bool compileDebug;
  bool with;
};

static Handle<Value> NodeParse (const Arguments&);
static std::deque<std::string> split(std::string &str, char delimiter);
static std::string filtered(std::string &js);
static std::string Parse (parse_args *pa);
extern "C" void init (Handle<Object>);


//_parse(open, close, filename, compileDebug);
Handle<Value> NodeParse(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() != 6) {
    std::stringstream sstm;
    sstm << "Parse requires 6 params (str, open, close, filename, compileDebug, with), got " << args.Length();
    std::string errMsg = sstm.str();
    return ThrowException(Exception::Error(
       String::New(errMsg.c_str())));
  } else {
    v8::String::Utf8Value arg_str(args[0]->ToString());
    std::string str = std::string(*arg_str);   

    v8::String::Utf8Value arg_open(args[1]->ToString());
    std::string open = std::string(*arg_open);   


    v8::String::Utf8Value arg_close(args[2]->ToString());
    std::string close = std::string(*arg_close);   
    
    v8::String::Utf8Value arg_filename(args[3]->ToString());
    std::string filename = std::string(*arg_filename);   

    Handle<Boolean> arg_compileDebug = args[4]->ToBoolean();
    bool compileDebug = (arg_compileDebug == v8::True());

    Handle<Boolean> arg_with = args[5]->ToBoolean();
    bool with = (arg_with == v8::True());
    parse_args *pa = new parse_args;
    pa->str = str;
    pa->open = open;
    pa->close = close;
    pa->filename = filename;
    pa->compileDebug = compileDebug;
    pa->with = with;
    std::string parseResult = Parse(pa);

    if(pa->err.length()) {
      return ThrowException(Exception::Error(
        String::New(pa->err.c_str())));
    }
    else {
      Handle<Value> result = String::New(parseResult.c_str());
        return scope.Close(result);;
    }
  }
}

static inline std::string resolveInclude(std::string name, std::string filename) {
  std::string path;

  //add extension ejs to name if it doesnt have one
  if(name.find('.') == std::string::npos) {
    name = name + ".ejs";
  }

  int lastSlashIndex = filename.rfind('/');
  if(lastSlashIndex > 0) {
    path = filename.substr(0, lastSlashIndex + 1) + name;
  } 
  else {
    path = name;
  }
  return path;
}

/*
JS String.split(delimiter)
*/
static inline std::deque<std::string> split(std::string &str, char delimiter) {
  std::deque<std::string> strings;
  std::stringstream f;
  std::string s;
  f << str;
  while (std::getline(f, s, delimiter)) {
    strings.push_back(s);
  }
  return strings;
}

/*
JS Array.join(delimiter)
*/
static inline std::string join(std::deque<std::string> &arr, char delimiter) {
  std::stringstream sstm;
  for(uint i=0; i<arr.size(); i++) {
    sstm << arr[i];
    if(i != arr.size() - 1) {
      sstm << delimiter;
    }
  }
  return sstm.str();
}

/*
function filtered(js) {
  return js.substr(1).split('|').reduce(function(js, filter){
    var parts = filter.split(':')
      , name = parts.shift()
      , args = parts.join(':') || '';
    if (args) args = ', ' + args;
    return 'filters.' + name + '(' + js + args + ')';
  });
};
*/
static std::string filtered(std::string &js) {
  js = js.substr(1, std::string::npos);
  std::deque<std::string> strings = split(js, '|');
  trim(strings[0]);
  for(uint i=1; i< strings.size(); i++) {
    //reduce params
    trim(strings[i]);
    std::string a = strings[i-1];
    std::string b = strings[i];
    std::deque<std::string> parts = split(b, ':');
    std::string name = parts.front();
    parts.pop_front();

    std::string args = parts.size() ? join(parts, ':') : "";
    if(args.length()) {
      args = ", " + args;
    }
    strings[i] = "filters." + name + "(" + a + args + ")";
  }
  
  return strings.back();
}

static std::string Parse(parse_args *pa) {
  std::string str = pa->str;
  std::string open = pa->open;
  std::string close = pa->close;
  std::string filename = pa->filename;
  bool compileDebug = pa->compileDebug;
  bool with = pa->with;

  /*
  buf += 'var buf = [];';
  if (false !== options._with) buf += '\nwith (locals || {}) { (function(){ ';
  buf += '\n buf.push(\''; 

  var lineno = 1;
  var consumeEOL = false;
  */
  std::stringstream buf;
  buf << "var buf = [];";
  if(with) {
    buf << "\nwith (locals || {}) { (function(){ ";
  }
  buf << "\n buf.push(\'"; 

  int lineno = 1;
  bool consumeEOL = false;

  /*
  for (var i = 0, len = str.length; i < len; ++i) {
    var stri = str[i];
    if (str.slice(i, open.length + i) == open) {
      i += open.length
  
      var prefix, postfix, line = (compileDebug ? '__stack.lineno=' : '') + lineno;
  */
  for(uint i=0; i<str.length(); i++) {
    if(strncmp(str.substr(i, open.length()).c_str(), open.c_str(), open.length()) == 0) {
      i += open.length();
      std::string prefix;
      std::string postfix;
      std::stringstream line_sstm;
      if(compileDebug) {
        line_sstm << "__stack.lineno=";
      }
      line_sstm << lineno;
      std::string line = line_sstm.str();
      /*
      switch (str[i]) {
        case '=':
          prefix = "', escape((" + line + ', ';
          postfix = ")), '";
          ++i;
          break;
        case '-':
          prefix = "', (" + line + ', ';
          postfix = "), '";
          ++i;
          break;
        default:
          prefix = "');" + line + ';';
          postfix = "; buf.push('";
      }
      */
      switch (str[i]) {
        case '=':
          prefix = "', escape((" + line + ", ";
          postfix = ")), '";
          i++;
          break;
        case '-':
          prefix = "', (" + line + ", ";
          postfix = "), '";
          i++;
          break;
        default:
          prefix = "');" + line + ";";
          postfix = "; buf.push('";
      }

      /*
      var end = str.indexOf(close, i)
        , js = str.substring(i, end)
        , start = i
        , include = null
        , n = 0;
      */ 
      uint start = i;
      uint end = str.find(close, i);
      uint n = 0;
      std::string include;
      std::string js = str.substr(i, end - i);

      /*
      if ('-' == js[js.length-1]){
        js = js.substring(0, js.length - 2);
        consumeEOL = true;
      }
      */
      if('-' == js[js.length() - 1]) {
        js = js.substr(0, js.length() - 2);
        consumeEOL = true;
      }

      /*
      if (0 == js.trim().indexOf('include')) {
        var name = js.trim().slice(7).trim();
        if (!filename) throw new Error('filename option is required for includes');
        var path = resolveInclude(name, filename);
        include = read(path, 'utf8');
        include = exports.parse(include, { filename: path, _with: false, open: open, close: close, compileDebug: compileDebug });
        buf += "' + (function(){" + include + "})() + '";
        js = '';
      }
      */
      trim(js);
      if(strncmp(js.c_str(), "include", 7) == 0) { //(0 == js.trim().indexOf('include'))
        std::string name = js.substr(7, js.length() - 7);
        trim(name);

        if(filename.length() == 0) {
          pa->err = "filename option is required for includes";
          return "";
        }

        std::string path = resolveInclude(name, filename);
        std::string line;
        std::ifstream include (path.c_str());
        if(include.is_open()) {
          
          // get length of file:
          include.seekg (0, include.end);
          int length = include.tellg();
          include.seekg (0, include.beg);

          char * buffer = new char [length + 1];
          // read data as a block:
          include.read (buffer,length);
          include.close();

          buffer[length] = '\0';

          parse_args *pia = new parse_args;
          pia->str = std::string(buffer);
          pia->open = open;
          pia->close = close;
          pia->filename = path;
          pia->compileDebug = compileDebug;
          pia->with = false;
          
          // ...buffer contains the entire file...
          delete[] buffer;

          std::string parsedFile = Parse(pia);
          
          if(pia->err.length()) {
            pa->err = pia->err;
            std::cout << "Error! " << pa->err << std::endl;
            return "";
          }
          else {
            buf << "' + (function(){" << parsedFile << "})() + '";
            js = "";
          }

          delete pia;
        }
        else {
          pa->err = "Unable to open file [" + path + "]";
          return "";
        }
      }

      /*
      while (~(n = js.indexOf("\n", n))) n++, lineno++;
      */
      while(str.find("\n", n) != std::string::npos) {
        n++;
        lineno++;
      }

      /*
      if (js.substr(0, 1) == ':') js = filtered(js);
      */
      if(js.substr(0,1) == ":") {
        js = filtered(js);
      }

      /*
      if (js) {
        if (js.lastIndexOf('//') > js.lastIndexOf('\n')) {
          js += '\n';
        } 
        buf += prefix;
        buf += js;
        buf += postfix;
      }
      i += end - start + close.length - 1;
      */
      if(js.length()) {
        if((int)(js.rfind("//")) > (int)(js.rfind("\n"))) {
          js += "\n";
        }
        buf << prefix << js << postfix;
      }
      i += end - start + close.length() - 1;
    /*
    } else if (stri == "\\") {
      buf += "\\\\";
    } else if (stri == "'") {
      buf += "\\'";
    } else if (stri == "\r") {
      // ignore
    } else if (stri == "\n") {
      if (consumeEOL) {
        consumeEOL = false;
      } else {
        buf += "\\n";
        lineno++;
      }
    } else {
      buf += stri;
    }
    */
    } else if (str[i] == '\\') {
      buf << "\\\\";
    } else if (str[i] == '\'') {
      buf << "\\'";
    } else if (str[i] == '\r') {
      // ignore
    } else if (str[i] == '\n') {
      if (consumeEOL) {
        consumeEOL = false;
      } else {
        buf << "\\n";
        lineno++;
      }
    } else {
      buf << str[i];
    }
  }
  
  /*
  if (false !== options._with) buf += "'); })();\n} \nreturn buf.join('');";
  else buf += "');\nreturn buf.join('');";
  return buf;
  */
  if(with) {
    buf << "'); })();\n} \nreturn buf.join('');";
  } else {
    buf << "');\nreturn buf.join('');";
  }
  return buf.str();
}

extern "C" void init (Handle<Object> target) {
  HandleScope scope;
  NODE_SET_METHOD(target, "parse", NodeParse);
}