<table width=800>
<tr><td width=200>

<table bgcolor=#000000 cellspacing=2 cellpadding=4 border=0 width=100%>
<tr><td bgcolor=#ffffff><h1>BlitzExample</h1></td></tr>
</table>
</td>

<tmpl:adverts>

<td width=200 valign=top>
<table bgcolor=#000000 cellspacing=2 cellpadding=2 border=0 width=100%>
<tr><td><font color=#ffffff><b>{title}</b></font></td></tr>
<tr><td bgcolor=#ffffff><small><a href="{url}">{body}</a></small>
</td></tr>
</table>

</tmpl:adverts>

</td>
</tr>

<tr valign=top>
<td width=200>
<table width=100% cellpadding=3>

<tmpl:sections>

<tr>
<td bgcolor=<tmpl:odd>#eeeeee</tmpl:odd><tmpl:even>#dddddd</tmpl:even>>
<font color=#ffffff><b>
<a href="/section.phtml?id={id}">{name}</a>
<tmpl:rip><font color=#999999>R.I.P.</font></font></tmpl:rip>
</td>
</tr>

</tmpl:sections>

</table>

<p><b>Users</b>: {num_total}<br>

<b>Online</b>: {num_online}<br>

<small>
<i>
<tmpl:users>
<a href="/user.phtml?id={id}">{name}</a>
</tmpl:users>
</i>
</small>

</small>
<p><b>{poll_title}</b><br>
<small>
{poll_question}
<small><br>
<form method=post>
<table>
<tmpl:poll_answers>
<tr valign=center><td><small><input type=radio name=a>{answer}<br></td></tr>
</tmpl:poll_answers>

<tr><td align=center><input type=submit name="OK" value="{poll_button}"></td></tr>
</table>
</form>
</td>

<td width=400 colspan=3>
<tmpl:news>
<b>{time} {title}</b><br>
<small>{short}<a href="/news.phtml?id={id}">[ read full story ]</a></small>
<br>
</tmpl:news>

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
