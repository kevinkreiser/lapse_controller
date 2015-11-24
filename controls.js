//get the key value query parameters from the current location
function query_parameters() {
  var query = window.location.search.substring(1);
  var params = query.split('&');
  var kvs = {};
  params.forEach(function (param) {
    var kv = param.split('=');
    var key = decodeURIComponent(kv[0]);
    var value = kv.length > 1 ? value = decodeURIComponent(kv[1]) : '';
    if(kvs[key] !== undefined)
      kvs[key].push(value);
    else
      kvs[key] = [value];
  });
  return kvs;
}

//what to do to who on button press
function configure(camera) {
  //get the update from div
  var sizes = [];
  var options = document.getElementById(camera + '.sizes').options;
  if(options.selectedIndex != -1) {    
    sizes.push(options[options.selectedIndex].text);
    for(var i = 0; i < options.length; i++)
      if(i != options.selectedIndex)
        sizes.push(options[i].text);
  }
  var config = {
    schedule: {
      enabled: document.getElementById(camera + '.enabled').checked,
      interval: document.getElementById(camera + '.interval').value,
      weekdays: [
        document.getElementById(camera + '.monday').checked,
        document.getElementById(camera + '.teusday').checked,
        document.getElementById(camera + '.wednesday').checked,
        document.getElementById(camera + '.thursday').checked,
        document.getElementById(camera + '.friday').checked,
        document.getElementById(camera + '.saturday').checked,
        document.getElementById(camera + '.sunday').checked
      ],
      daily_start_time: document.getElementById(camera + '.start').value,
      daily_end_time: document.getElementById(camera + '.end').value
    },
    camera: {
      jpeg_quality: document.getElementById(camera + '.quality').value,
      picture_sizes: sizes
    }
  };
  //send the update
  var request = new XMLHttpRequest();
  request.onreadystatechange = function () {
    if(request.readyState == 4)
      alert(request.status + ": " + request.responseText);
  };
  var kvs = query_parameters();
  var pass_key = kvs['pass_key'] !== undefined ? kvs['pass_key'][0] : '';
  var query = 'configure?camera=' + encodeURIComponent(camera) +
    		  '&info=' + encodeURIComponent(JSON.stringify(config)) +
    		  '&pass_key=' + encodeURIComponent(pass_key);
  request.open('GET', query, true);
  request.send();
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

//make a dom select element with the following values and i
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

//run over the items or not
if(cameras === undefined)
  var cameras = [];
cameras.forEach(function(camera) {
  //throw json into an iframe to let firefox jsonview plugin pretty print it
  var iframe = document.createElement('iframe');
  iframe.src = 'data:application/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(camera));
  document.body.appendChild(iframe);
  document.body.appendChild(document.createElement('br'));
  //signal we cant make sense of this cameras settings
  var form = document.createElement('form');
  if(camera['endpoint'] === undefined || camera['settings'] === undefined || 
     camera['settings']['schedule'] === undefined || camera['settings']['camera'] === undefined) {
    form.appendChild(document.createTextNode("Incompatible settings version..."));
    document.body.appendChild(form);
    document.body.appendChild(document.createElement('br'));
    document.body.appendChild(document.createElement('br'));
    return;
  }
  //grab a few properties we'll use throughout
  var name = camera['endpoint'];
  var sch = camera['settings']['schedule'];
  var cam = camera['settings']['camera']; 
  //make changable config
  form.appendChild(input('checkbox', name + '.enabled', {checked: sch['enabled']}));
  form.appendChild(document.createTextNode("enabled"));
  form.appendChild(document.createElement('br'));
  form.appendChild(document.createTextNode("interval "));
  form.appendChild(input('number', name + '.interval', {value: sch['interval'], min: 1, max: 300}));
  form.appendChild(document.createElement('br'));
  form.appendChild(input('checkbox', name + '.monday', {checked: sch['weekdays'][0]}));
  form.appendChild(document.createTextNode("monday   "));
  form.appendChild(input('checkbox', name + '.teusday', {checked: sch['weekdays'][1]}));
  form.appendChild(document.createTextNode("teusday   "));
  form.appendChild(input('checkbox', name + '.wednesday', {checked: sch['weekdays'][2]}));
  form.appendChild(document.createTextNode("wednesday   "));
  form.appendChild(input('checkbox', name + '.thursday', {checked: sch['weekdays'][3]}));
  form.appendChild(document.createTextNode("thursday   "));
  form.appendChild(input('checkbox', name + '.friday', {checked: sch['weekdays'][4]}));
  form.appendChild(document.createTextNode("friday   "));
  form.appendChild(input('checkbox', name + '.saturday', {checked: sch['weekdays'][5]}));
  form.appendChild(document.createTextNode("saturday   "));
  form.appendChild(input('checkbox', name + '.sunday', {checked: sch['weekdays'][6]}));
  form.appendChild(document.createTextNode("sunday"));
  form.appendChild(document.createElement('br'));
  form.appendChild(document.createTextNode("daily start time "));
  form.appendChild(input('text', name + '.start', {value: sch['daily_start_time']}));
  form.appendChild(document.createElement('br'));
  form.appendChild(document.createTextNode("daily end time "));
  form.appendChild(input('text', name + '.end', {value: sch['daily_end_time']}));
  form.appendChild(document.createElement('br'));
  form.appendChild(document.createTextNode("jpeg quality "));
  form.appendChild(input('number', name + '.quality', {value: cam['jpeg_quality'], min: 1, max: 100}));
  form.appendChild(document.createElement('br'));
  form.appendChild(document.createTextNode("picture sizes "));
  form.appendChild(select(name + '.sizes', cam['picture_sizes']));
  form.appendChild(document.createElement('br'));
  var link = document.createElement('a');
  link.appendChild(document.createTextNode(camera['uuid'] + ' photos'));
  link.href = 'cameras/' + camera['uuid'] + '/';
  form.appendChild(link);
  document.body.appendChild(form);
  //make update button
  var button = document.createElement('button');
  button.onclick = configure.bind(undefined, name);
  button.appendChild(document.createTextNode('update'));
  document.body.appendChild(button);
  document.body.appendChild(document.createElement('br'));
  document.body.appendChild(document.createElement('br'));
});