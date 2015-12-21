//get the key value query parameters from the current location
function query_parameters(flatten = false) {
  var query = window.location.search.substring(1);
  var params = query.split('&');
  var kvs = {};
  params.forEach(function (param) {
    var kv = param.split('=');
    var key = decodeURIComponent(kv[0]);
    var value = kv.length > 1 ? value = decodeURIComponent(kv[1]) : '';
    //if it doesnt have multiple values its not an array
    if(flatten) {
      if(kvs[key] === undefined)
        kvs[key] = value;
      else if(Object.prototype.toString.call(kvs[key]) !== '[object Array]')
        kvs[key] = [kvs[key], value]
      else
        kvs[key].push(value);
    }//everything is arrays
    else {
      if(kvs[key] !== undefined)
        kvs[key].push(value);
      else
        kvs[key] = [value];
    }
  });
  return kvs;
}

//make a dom input element with the following properties and id
function input(type, id, properties) {
  var i = document.createElement('input');
  i.type = type;
  i.id = id;
  for (var property in properties)
    if (properties.hasOwnProperty(property))
      i[property] = properties[property];
  return i;
}

//make a dom select element with the following values and id
function select(id, values) {
  var s = document.createElement('select');
  s.id = id;
  values.forEach(function (size) {
    var option = document.createElement("option");
    option.value = size;
    option.text = size;
    s.appendChild(option);
  });
  return s;
}
