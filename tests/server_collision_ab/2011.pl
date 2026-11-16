# Task-owned stationary production queries. Reuses the qualified Rat origin/LoS pair.
use Time::HiRes qw(time);
my $finished = 0;
sub EVENT_SPAWN { quest::settimer("server_collision", 1); }
sub EVENT_TIMER {
    return unless $timer eq "server_collision";
    if (-e "/runtime/stop-map-validation") {
        quest::stoptimer("server_collision");
        quest::debug("MAP_SERVER_PROBE_STOPPED");
        return;
    }
    return if $finished;
    $npc->GMMove(297.75,344,3.75,0,0);
    # Offset zero plus FindBestZHeightAdjust=1 searches below the overhead roof.
    # These three floor samples are NOT a qualified height-transition route.
    for my $t (["start",297.75,344,3.75], ["clear",288,344,3.75], ["blocked",320,344,3.75]) {
        my ($id,$x,$y,$z)=@$t;
        my $ground=$npc->FindGroundZ($x,$y,0);
        my $los=$npc->CheckLoSToLoc($x,$y,$z,6) ? 1 : 0;
        my $eye=unpack("f",pack("f",3.75+unpack("f",pack("f",3*unpack("f",pack("f",0.9))))));
        quest::debug("SERVER_COLLISION_QUERY id=$id time=".time()." npc=".$npc->GetNPCTypeID()." entity=".$npc->GetID()." from=297.75,344,3.75 size=6 origin=$x,$y,3.75 adjusted=$x,$y,4.75 eyes=297.75,344,$eye to=$x,$y,$z target_size=6 target_eyes=$x,$y,5.25 ground=$ground los=$los");
    }
    quest::debug("SERVER_COLLISION_QUERIES_DONE");
    $finished=1;
}
