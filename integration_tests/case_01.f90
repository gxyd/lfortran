program case_01
implicit none
integer :: i, out
i = 4

select case(i)
    case (1)
        out = 10
        print *, "1"
    case (2)
        out = 20
        print *, "2"
    case (3)
        out = 30
        print *, "3"
    case (4)
        out = 40
        print *, "4"
end select

if (out /= 40) error stop

selectcase(i)
    case (1)
        out = 11
        print *, "1"
    case (2,3,4)
        out = 22
        print *, "2,3,4"
end select

if (out /= 22) error stop

end
