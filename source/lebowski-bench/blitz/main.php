<?

//include_once('../timer.php');
//timer_start('full');

if(!extension_loaded('blitz') && !dl('blitz.so')) {
    die("unable to load blitz extension\n");
}

include_once('../data.inc');
$_tpl_path = './';

/*****************************************************************************/
class TestView extends Blitz {
/*****************************************************************************/
    var $data;
    var $path;

/*****************************************************************************/
    function TestView($path,$data) {
/*****************************************************************************/
        $this->data = $data;
        $this->path = $path;
        parent::Blitz($path.'main.tpl');
        $this->init_root_vars();
    }

/*****************************************************************************/
    function init_root_vars() {
/*****************************************************************************/
        $stat = & $this->data['STAT'];
		$total = isset($stat['TOTAL']) ? (int)$stat['TOTAL'] : 0;
		$online = isset($stat['ONLINE']) ? count($stat['ONLINE']) : 0;
		$this->set(
            array(
                'total_users'   => $total,
                'total_online_users'    => $online,
                'poll_title' => $this->data['POLL']['TITLE'],
                'poll_question' =>  $this->data['POLL']['QUESTION'],
                'poll_button' => $this->data['POLL']['BUTTON'],
            )
		);
    }

/*****************************************************************************/
    function adverts() {
/*****************************************************************************/
        $random_keys =  array_rand($this->data['ADVERTS'],3);
        $result = '';
        $tpl_name = $this->path.'ushi_item.tpl';

        foreach($random_keys as $i) {
            $idata = & $this->data['ADVERTS'][$i];
            $result .= $this->include($tpl_name,
                array(
                    'section' => $idata[0],
                    'title' => $idata[1],
                    'url'   => $idata[2],
                )
            );
        }

        return $result;
    }

/*****************************************************************************/
    function sections() {
/*****************************************************************************/
        $result = '';
        $n_sections = count($this->data['SECTIONS']);
        $tpl_name = $this->path.'section_item.tpl';
        for($i=0; $i<$n_sections; $i++) {
            $idata = & $this->data['SECTIONS'][$i];

            $result .= $this->include(
                $tpl_name,
                array(
                    'id'   => $idata[0],
                    'name' => $idata[1],
                    'rip'  => $idata[2],
                    'odd'  => $i%2,
                )
            );
        }
        return $result;
    }

/*****************************************************************************/
    function news() {
/*****************************************************************************/
        $result = '';
        $n_news = count($this->data['NEWS']);
        $tpl_name = $this->path.'news_item.tpl';
        for($i=0; $i<$n_news; $i++) {
            $idata = & $this->data['NEWS'][$i];
            $result .= $this->include($tpl_name,
                array(
                    'id'    => $idata[1],
                    'time'  => $idata[0],
                    'title' => $idata[2],
                    'short' => $idata[3],
                )
            );
        }

        return $result;
    }

/******************************************************************************/
    function users_online() {
/******************************************************************************/
        $result = '';
        $users = & $this->data['STAT']['ONLINE'];
        $n_users = count($users);
        $tpl_name = $this->path.'users_online_item.tpl';
        for($i=0; $i<$n_users; $i++) {
            $result .= $this->include($tpl_name,
               array(
                    'id'    => $users[$i][0],
                    'name'  => $users[$i][1],
               )
            );
        }

        return $result;        
    }

/******************************************************************************/
    function poll_answers() {
/******************************************************************************/
        $result = '';
        $n_sections = count($this->data['POLL']['ANSWERS']);
        $tpl_name = $this->path.'poll_answer.tpl';
        for($i=0; $i<$n_sections; $i++) {
            $result .= $this->include($tpl_name,
                array(
                    'id'   => $i,
                    'answer' => $this->data['POLL']['ANSWERS'][$i],
                )
            );
        }

        return $result;
    }
}

/*****************************************************************************/
/* main 																     */
/*****************************************************************************/

$T = & new TestView($_tpl_path,$_DATA);
echo $T->parse();
//timer_stop('full');
//echo(timer_dump());

?>
