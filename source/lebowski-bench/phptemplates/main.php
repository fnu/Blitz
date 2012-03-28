<?

include_once('../data.inc');
$tpl = tmpl_open('main.tpl');

// root vars
tmpl_set($tpl,
    array (
        'num_total' => $_STAT['TOTAL'],
        'num_online'=> count($_STAT['ONLINE']),
        'poll_title' => $_POLL['TITLE'],
        'poll_question' =>  $_POLL['QUESTION'],
        'poll_button' => $_POLL['BUTTON'],
    )
);

// adverts
$random_keys =  array_rand($_ADVERTS,3);
foreach($random_keys as $i) {
    tmpl_iterate($tpl,'/adverts');
    tmpl_set($tpl,'/adverts',
		array(
			'url' => $_ADVERTS[$i][2],
            'title' => $_ADVERTS[$i][0],
            'body' => $_ADVERTS[$i][1]
		)
    );
}

// sections
$n_sections = count($_SECTIONS);
for($i=0; $i<$n_sections; $i++) {
	$idata = & $_SECTIONS[$i];
    tmpl_iterate($tpl,'/sections');
    tmpl_set($tpl,'/sections',
		array(
			'id'   => $idata[0],
			'name' => $idata[1],
		)
    );
    if ($idata[2]) {
		tmpl_iterate($tpl,'/sections/rip');
    }
    tmpl_iterate($tpl,$i%2 ? '/sections/odd' : '/sections/even');
}

// users
$users = $_STAT['ONLINE'];
$n_users = count($users);
for($i=0; $i<$n_users; $i++) {
     tmpl_iterate($tpl,'/users');
     tmpl_set($tpl,'/users',
         array(
             'id'    => $users[$i][0],
             'name'  => $users[$i][1],
         )
     );
}

// news
$n_news = count($_NEWS);
for($i=0; $i<$n_news; $i++) {
    tmpl_iterate($tpl,'/news');
    tmpl_set($tpl,'/news',
        array(
            'id'    => $_NEWS[$i][1],
            'time'  => $_NEWS[$i][0],
            'title' => $_NEWS[$i][2],
            'short' => $_NEWS[$i][3],
        )
    );
}

// poll
$n_answers = count($_POLL['ANSWERS']);
for($i=0; $i<$n_answers; $i++) {
    tmpl_iterate($tpl,'/poll_answers');
    tmpl_set($tpl,'/poll_answers',
         array(
             'id'   => $i,
             'answer' => $_POLL['ANSWERS'][$i]
         )
    );
}

echo tmpl_parse($tpl);

?>
