<table width=800>
<tr><td width=200>

<table bgcolor=#000000 cellspacing=2 cellpadding=4 border=0 width=100%>
<tr><td bgcolor=#ffffff><h1>BlitzExample</h1></td></tr>
</table>
</td>
{{ adverts }}
</tr>

<tr valign=top>
<td width=200>
<table width=100% cellpadding=3>{{ sections }}</table>

<p><b>Users</b>: {{ $total_users }}<br>
<b>Online</b>: {{ $total_online_users }}<br>
<small>
<i>{{ users_online }}</i>
</small>

</small>
<p><b>{{ $poll_title }}</b><br>
<small>
{{ $poll_question }}
<small><br>
<form method=post>
<table>
{{ poll_answers }}
<tr><td align=center><input type=submit name="OK" value="{{ $poll_button }}"></td></tr>
</table>
</form>
</td>

<td width=400 colspan=3>
{{ news }}
</td>

</tr>
<tr>
<td colspan=4 align=center>
<hr>
<small>
<i>BlitzExample (Copyleft) Alexey A. Rybak, 2005.<br>
Texts are taken from IMDB.com, Memorable Quotes from "The Big Lebowski" (Ethan & Joel Coen, 1998). <br>
You are welcome to send any suggestions or comments to <b>raa@phpclub.net</b> </i>

</td>
</tr>
</table>
