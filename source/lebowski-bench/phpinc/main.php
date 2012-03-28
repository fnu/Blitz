<?  

include_once('../data.inc'); 

?>

<table width=800>
<tr><td width=200>

<table bgcolor=#000000 cellspacing=2 cellpadding=4 border=0 width=100%>
<tr><td bgcolor=#ffffff><h1>BlitzExample</h1></td></tr>
</table>
</td>

<? 
$random_keys =  array_rand($_DATA['ADVERTS'],3);

foreach($random_keys as $i) {
	$idata = & $_DATA['ADVERTS'][$i];
    include('advert_item.inc');
}
?>

</td>
</tr>

<tr valign=top>
<td width=200>
<table width=100% cellpadding=3>

<?
$n_sections = count($_DATA['SECTIONS']);

for($i=0; $i<$n_sections; $i++) {
    $sdata = & $_DATA['SECTIONS'][$i];
    include('section_item.inc');
}
?>

</table>

<p><b>Users</b>: <?=$_DATA['STAT']['TOTAL']; ?><br>

<?
$n_online = count($_DATA['STAT']['ONLINE']); 
?>

<b>Online</b>: <?=$n_online;?><br>

<small>
<i>
<? 
    for($i=0;$i<$n_online; $i++) { 
        $udata = $_DATA['STAT']['ONLINE'][$i]; 
		include('user_item.inc');
    }

?>
</i>
</small>

</small>
<p><b><?=$_POLL['TITLE'];?></b><br>
<small>
<?=$_POLL['QUESTION'];?>
<small><br>
<form method=post>
<table>
<? 

$n_answer = count($_POLL['ANSWERS']);
for($i=0;$i<$n_answer;$i++) {
	include('poll_item.inc');
}

?>
<tr><td align=center><input type=submit name="OK" value="<?=$_POLL['BUTTON'];?>"></td></tr>
</table>
</form>
</td>

<td width=400 colspan=3>
<? 
$n_news = count($_NEWS);
for($i=0;$i<$n_news;$i++) { 
	include('news_item.inc');
}
?>

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
