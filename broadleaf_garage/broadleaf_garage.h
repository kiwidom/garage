const String htmlStart = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";

const String header = R"=====(
<!DOCTYPE HTML>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<style>
html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}
.button { background-color: #195B6A; border: none; color: white; padding: 16px 40px; text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}
.button2 {color: #77878A; margin: 10px; font-size: 12px; }
.button3 { background-color: #555; color: #fff; border: none; color: white; padding: 16px 40px; text-decoration: none; font-size: 30px; margin: 2px; }
</style>
</head>
<body>

)=====";


const String end = R"=====(

</body>
</html>
)====="; 
